package main

// store.go —— 服务端 IM 持久化层，基于 bbolt（纯 Go、无 CGO、事务型）。
// 所有"读计数->校验->写入"的限额逻辑都放在单个 Update 事务里，
// 借 bbolt 的单写者串行保证消除 TOCTOU 竞态。

import (
	"crypto/md5"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"sort"
	"strings"
	"sync/atomic"

	bolt "go.etcd.io/bbolt"
)

const (
	MaxGroupsPerUser  = 5  // 每个用户最多创建的组数
	MaxMembersPerGrp  = 10 // 每个组最多成员数（含群主）
	storeDefaultPath  = "convnet.db"
)

// bucket 名称
var (
	bkUsers        = []byte("users")             // userID -> UserRecord
	bkAccountIndex = []byte("account_index")     // lower(account) -> userID
	bkFriends      = []byte("friends")           // userID -> []uint64（对称邻接表）
	bkFriendReqs   = []byte("friend_reqs")       // toUserID -> []FriendRequest
	bkGroups       = []byte("groups")            // groupID -> GroupRecord
	bkGroupOwner   = []byte("group_owner_index") // ownerID -> []uint64(groupID)  ==> <=5 计数器
	bkGroupReqs    = []byte("group_reqs")        // groupID -> []GroupJoinRequest
	bkAcl          = []byte("acl")               // userID -> AclRecord
	bkOffline      = []byte("offline_msgs")      // userID -> []OfflineMsg
	bkMeta         = []byte("meta")              // 杂项：groupIDCounter/userIDCounter 等
)

var allBuckets = [][]byte{
	bkUsers, bkAccountIndex, bkFriends, bkFriendReqs, bkGroups,
	bkGroupOwner, bkGroupReqs, bkAcl, bkOffline, bkMeta,
}

// Store 封装 bbolt 数据库。
type Store struct {
	db             *bolt.DB
	groupIDCounter uint64
	userIDCounter  uint64
}

var store *Store // 服务端全局单例；客户端模式下为 nil（IM handler 仅服务端调用）

// StoreInit 打开数据库并建好所有 bucket。仅服务端模式调用。
func StoreInit(path string) error {
	if path == "" {
		path = storeDefaultPath
	}
	db, err := bolt.Open(path, 0600, nil)
	if err != nil {
		return err
	}
	s := &Store{db: db}
	err = db.Update(func(tx *bolt.Tx) error {
		for _, b := range allBuckets {
			if _, e := tx.CreateBucketIfNotExists(b); e != nil {
				return e
			}
		}
		// 恢复计数器
		meta := tx.Bucket(bkMeta)
		if v := meta.Get([]byte("groupIDCounter")); v != nil {
			s.groupIDCounter = binary.BigEndian.Uint64(v)
		}
		if v := meta.Get([]byte("userIDCounter")); v != nil {
			s.userIDCounter = binary.BigEndian.Uint64(v)
		}
		// 稳健起见：userIDCounter 不小于 users 桶中已存在的最大 userID，
		// 避免与历史（含旧 UUID 路径）分配的 ID 冲突。
		tx.Bucket(bkUsers).ForEach(func(k, _ []byte) error {
			if len(k) == 8 {
				if id := binary.BigEndian.Uint64(k); id > s.userIDCounter {
					s.userIDCounter = id
				}
			}
			return nil
		})
		return nil
	})
	if err != nil {
		db.Close()
		return err
	}
	store = s
	return nil
}

// ---- 小工具 ----

func itob(v uint64) []byte {
	b := make([]byte, 8)
	binary.BigEndian.PutUint64(b, v)
	return b
}

func addUnique(list []uint64, v uint64) ([]uint64, bool) {
	for _, x := range list {
		if x == v {
			return list, false
		}
	}
	list = append(list, v)
	sort.Slice(list, func(i, j int) bool { return list[i] < list[j] })
	return list, true
}

func removeVal(list []uint64, v uint64) ([]uint64, bool) {
	out := list[:0:0]
	removed := false
	for _, x := range list {
		if x == v {
			removed = true
			continue
		}
		out = append(out, x)
	}
	return out, removed
}

func getJSON(b *bolt.Bucket, key []byte, out interface{}) bool {
	raw := b.Get(key)
	if raw == nil {
		return false
	}
	return json.Unmarshal(raw, out) == nil
}

func putJSON(b *bolt.Bucket, key []byte, v interface{}) error {
	raw, err := json.Marshal(v)
	if err != nil {
		return err
	}
	return b.Put(key, raw)
}

// ===================== 用户 =====================

// UpsertUser 登记/更新用户身份（登录时调用）。
func (s *Store) UpsertUser(userID uint64, publicID, nick, mac string) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkUsers)
		rec := UserRecord{}
		getJSON(b, itob(userID), &rec)
		rec.UserID = userID
		rec.PublicID = publicID
		if nick != "" {
			rec.Nick = nick
		}
		if mac != "" {
			rec.Mac = mac
		}
		rec.CvnIP = GetCvnIPstring(int(userID))
		return putJSON(b, itob(userID), &rec)
	})
}

func (s *Store) GetUser(userID uint64) (UserRecord, bool) {
	var rec UserRecord
	ok := false
	s.db.View(func(tx *bolt.Tx) error {
		ok = getJSON(tx.Bucket(bkUsers), itob(userID), &rec)
		return nil
	})
	return rec, ok
}

// publicIDForAccount 由账号 + userID 生成 PublicID（保持 md5:userID 格式）。
func publicIDForAccount(account string, uid uint64) string {
	h := md5.Sum([]byte(account))
	return hex.EncodeToString(h[:]) + ":" + Inttostr(int(uid))
}

// CreateAccount 注册账号。单事务内校验账号唯一、分配 userID、落库 + 建索引。
func (s *Store) CreateAccount(account, passHash, nick string) (UserRecord, error) {
	var rec UserRecord
	accKey := []byte(strings.ToLower(strings.TrimSpace(account)))
	if len(accKey) == 0 {
		return rec, errors.New("账号不能为空")
	}
	err := s.db.Update(func(tx *bolt.Tx) error {
		ai := tx.Bucket(bkAccountIndex)
		if ai.Get(accKey) != nil {
			return errors.New("账号已存在")
		}
		uid := atomic.AddUint64(&s.userIDCounter, 1)
		rec = UserRecord{
			UserID:   uid,
			PublicID: publicIDForAccount(account, uid),
			Account:  account,
			PassHash: passHash,
			Nick:     nick,
			CvnIP:    GetCvnIPstring(int(uid)),
		}
		if err := putJSON(tx.Bucket(bkUsers), itob(uid), &rec); err != nil {
			return err
		}
		if err := ai.Put(accKey, itob(uid)); err != nil {
			return err
		}
		return tx.Bucket(bkMeta).Put([]byte("userIDCounter"), itob(s.userIDCounter))
	})
	return rec, err
}

// AllUsers 返回所有已注册用户（按 userID 排序），供管理后台列表。
func (s *Store) AllUsers() []UserRecord {
	var res []UserRecord
	s.db.View(func(tx *bolt.Tx) error {
		return tx.Bucket(bkUsers).ForEach(func(k, v []byte) error {
			var u UserRecord
			if json.Unmarshal(v, &u) == nil {
				res = append(res, u)
			}
			return nil
		})
	})
	sort.Slice(res, func(i, j int) bool { return res[i].UserID < res[j].UserID })
	return res
}

// SetUserRelay 设置某用户的中继权限与限速（管理后台）。
func (s *Store) SetUserRelay(userID uint64, blocked bool, limitKBps int) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkUsers)
		var u UserRecord
		if !getJSON(b, itob(userID), &u) {
			return errors.New("用户不存在")
		}
		u.RelayBlocked = blocked
		u.RelayLimitKBps = limitKBps
		return putJSON(b, itob(userID), &u)
	})
}

// SetGlobalRelayLimit / GlobalRelayLimit 全局中继限速（KB/s，0=不限）。
func (s *Store) SetGlobalRelayLimit(kbps int) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		return tx.Bucket(bkMeta).Put([]byte("globalRelayKBps"), itob(uint64(kbps)))
	})
}

func (s *Store) GlobalRelayLimit() int {
	v := 0
	s.db.View(func(tx *bolt.Tx) error {
		if raw := tx.Bucket(bkMeta).Get([]byte("globalRelayKBps")); raw != nil {
			v = int(binary.BigEndian.Uint64(raw))
		}
		return nil
	})
	return v
}

// GetUserByAccount 按账号查用户（含 PassHash，用于登录校验）。
func (s *Store) GetUserByAccount(account string) (UserRecord, bool) {
	var rec UserRecord
	ok := false
	accKey := []byte(strings.ToLower(strings.TrimSpace(account)))
	s.db.View(func(tx *bolt.Tx) error {
		uidb := tx.Bucket(bkAccountIndex).Get(accKey)
		if uidb == nil {
			return nil
		}
		uid := binary.BigEndian.Uint64(uidb)
		ok = getJSON(tx.Bucket(bkUsers), itob(uid), &rec)
		return nil
	})
	return rec, ok
}

// SearchUsers 按昵称/账号子串（不分大小写）或 userID 精确匹配搜索，排除自己。
func (s *Store) SearchUsers(keyword string, selfID uint64) []UserRecord {
	kw := strings.ToLower(strings.TrimSpace(keyword))
	var res []UserRecord
	if kw == "" {
		return res
	}
	s.db.View(func(tx *bolt.Tx) error {
		return tx.Bucket(bkUsers).ForEach(func(k, v []byte) error {
			var rec UserRecord
			if json.Unmarshal(v, &rec) != nil || rec.UserID == selfID {
				return nil
			}
			if strings.Contains(strings.ToLower(rec.Nick), kw) ||
				strings.Contains(strings.ToLower(rec.Account), kw) ||
				Inttostr(int(rec.UserID)) == kw ||
				strings.HasPrefix(strings.ToLower(rec.PublicID), kw) {
				res = append(res, rec)
			}
			return nil
		})
	})
	return res
}

// ===================== 好友 =====================

func readFriends(b *bolt.Bucket, userID uint64) []uint64 {
	var list []uint64
	getJSON(b, itob(userID), &list)
	return list
}

func (s *Store) AreFriends(a, b uint64) bool {
	res := false
	s.db.View(func(tx *bolt.Tx) error {
		for _, f := range readFriends(tx.Bucket(bkFriends), a) {
			if f == b {
				res = true
				break
			}
		}
		return nil
	})
	return res
}

func (s *Store) GetFriends(userID uint64) []uint64 {
	var list []uint64
	s.db.View(func(tx *bolt.Tx) error {
		list = readFriends(tx.Bucket(bkFriends), userID)
		return nil
	})
	return list
}

// AddFriendPair 对称加好友。
func (s *Store) AddFriendPair(a, b uint64) error {
	if a == b {
		return errors.New("cannot befriend self")
	}
	return s.db.Update(func(tx *bolt.Tx) error {
		fb := tx.Bucket(bkFriends)
		la := readFriends(fb, a)
		lb := readFriends(fb, b)
		la, _ = addUnique(la, b)
		lb, _ = addUnique(lb, a)
		if err := putJSON(fb, itob(a), la); err != nil {
			return err
		}
		return putJSON(fb, itob(b), lb)
	})
}

// RemoveFriendPair 对称删好友。
func (s *Store) RemoveFriendPair(a, b uint64) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		fb := tx.Bucket(bkFriends)
		la, _ := removeVal(readFriends(fb, a), b)
		lb, _ := removeVal(readFriends(fb, b), a)
		if err := putJSON(fb, itob(a), la); err != nil {
			return err
		}
		return putJSON(fb, itob(b), lb)
	})
}

// ---- 好友申请（离线保留）----

func (s *Store) AddFriendRequest(to uint64, req FriendRequest) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkFriendReqs)
		var list []FriendRequest
		getJSON(b, itob(to), &list)
		// 去重：同一 from 只保留最新
		out := list[:0:0]
		for _, r := range list {
			if r.FromUserID != req.FromUserID {
				out = append(out, r)
			}
		}
		out = append(out, req)
		return putJSON(b, itob(to), out)
	})
}

// PeekFriendRequests 读取但不清空——申请保留至 accept/decline 显式移除，避免丢失。
func (s *Store) PeekFriendRequests(to uint64) []FriendRequest {
	var list []FriendRequest
	s.db.View(func(tx *bolt.Tx) error {
		getJSON(tx.Bucket(bkFriendReqs), itob(to), &list)
		return nil
	})
	return list
}

func (s *Store) RemoveFriendRequest(to, from uint64) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkFriendReqs)
		var list []FriendRequest
		getJSON(b, itob(to), &list)
		out := list[:0:0]
		for _, r := range list {
			if r.FromUserID != from {
				out = append(out, r)
			}
		}
		return putJSON(b, itob(to), out)
	})
}

// ===================== 用户组 =====================

// CreateGroup 建群，单事务内强制 <=5。joinPassHash 为空表示入群需审批。
func (s *Store) CreateGroup(ownerID uint64, name, joinPassHash string) (GroupRecord, error) {
	var g GroupRecord
	err := s.db.Update(func(tx *bolt.Tx) error {
		oi := tx.Bucket(bkGroupOwner)
		var owned []uint64
		getJSON(oi, itob(ownerID), &owned)
		if len(owned) >= MaxGroupsPerUser {
			return errors.New("最多创建5个组")
		}
		gid := atomic.AddUint64(&s.groupIDCounter, 1)
		g = GroupRecord{
			GroupID:      gid,
			Name:         name,
			OwnerID:      ownerID,
			Members:      []uint64{ownerID},
			CreatedTs:    nowUnix(),
			JoinPassword: joinPassHash,
		}
		if err := putJSON(tx.Bucket(bkGroups), itob(gid), &g); err != nil {
			return err
		}
		owned = append(owned, gid)
		if err := putJSON(oi, itob(ownerID), owned); err != nil {
			return err
		}
		// 持久化 groupIDCounter
		return tx.Bucket(bkMeta).Put([]byte("groupIDCounter"), itob(s.groupIDCounter))
	})
	return g, err
}

func (s *Store) GetGroup(groupID uint64) (GroupRecord, bool) {
	var g GroupRecord
	ok := false
	s.db.View(func(tx *bolt.Tx) error {
		ok = getJSON(tx.Bucket(bkGroups), itob(groupID), &g)
		return nil
	})
	return g, ok
}

// SearchGroups 按组名子串（不分大小写）或 groupID 精确匹配。
func (s *Store) SearchGroups(keyword string) []GroupRecord {
	kw := strings.ToLower(strings.TrimSpace(keyword))
	var res []GroupRecord
	if kw == "" {
		return res
	}
	s.db.View(func(tx *bolt.Tx) error {
		return tx.Bucket(bkGroups).ForEach(func(k, v []byte) error {
			var g GroupRecord
			if json.Unmarshal(v, &g) != nil {
				return nil
			}
			if strings.Contains(strings.ToLower(g.Name), kw) || Inttostr(int(g.GroupID)) == kw {
				res = append(res, g)
			}
			return nil
		})
	})
	return res
}

// AddGroupMember 入群，单事务内强制 <=10 且去重。返回更新后的组。
func (s *Store) AddGroupMember(groupID, userID uint64) (GroupRecord, error) {
	var g GroupRecord
	err := s.db.Update(func(tx *bolt.Tx) error {
		gb := tx.Bucket(bkGroups)
		if !getJSON(gb, itob(groupID), &g) {
			return errors.New("组不存在")
		}
		for _, m := range g.Members {
			if m == userID {
				return errors.New("已在组内")
			}
		}
		if len(g.Members) >= MaxMembersPerGrp {
			return errors.New("该组已满（最多10人）")
		}
		g.Members = append(g.Members, userID)
		return putJSON(gb, itob(groupID), &g)
	})
	return g, err
}

// RemoveGroupMember 退群。群主退出则解散整个组。
// 返回：更新后的组、受影响的成员集合（用于推送）、是否解散。
func (s *Store) RemoveGroupMember(groupID, userID uint64) (GroupRecord, []uint64, bool, error) {
	var g GroupRecord
	var affected []uint64
	disbanded := false
	err := s.db.Update(func(tx *bolt.Tx) error {
		gb := tx.Bucket(bkGroups)
		if !getJSON(gb, itob(groupID), &g) {
			return errors.New("组不存在")
		}
		affected = append([]uint64{}, g.Members...)
		if userID == g.OwnerID {
			// 解散：删除组、清理群主索引与入群申请
			disbanded = true
			if err := gb.Delete(itob(groupID)); err != nil {
				return err
			}
			oi := tx.Bucket(bkGroupOwner)
			var owned []uint64
			getJSON(oi, itob(g.OwnerID), &owned)
			owned, _ = removeVal(owned, groupID)
			if err := putJSON(oi, itob(g.OwnerID), owned); err != nil {
				return err
			}
			return tx.Bucket(bkGroupReqs).Delete(itob(groupID))
		}
		newMembers, removed := removeVal(g.Members, userID)
		if !removed {
			return errors.New("不在组内")
		}
		g.Members = newMembers
		g.Admins, _ = removeVal(g.Admins, userID) // 退群同时移除其管理员身份
		return putJSON(gb, itob(groupID), &g)
	})
	return g, affected, disbanded, err
}

// GroupManage 群管理操作（权限校验+变更在单事务内）。
// action: kick/grant/revoke/transfer/handover/disband。
// 返回：更新后的组、变更前成员集合(用于推送刷新)、是否解散、错误。
func (s *Store) GroupManage(gid, actor, target uint64, action string) (GroupRecord, []uint64, bool, error) {
	var g GroupRecord
	var notify []uint64
	disbanded := false
	err := s.db.Update(func(tx *bolt.Tx) error {
		gb := tx.Bucket(bkGroups)
		if !getJSON(gb, itob(gid), &g) {
			return errors.New("组不存在")
		}
		isOwner := actor == g.OwnerID
		isAdmin := isOwner
		for _, a := range g.Admins {
			if a == actor {
				isAdmin = true
			}
		}
		isMember := func(id uint64) bool {
			for _, m := range g.Members {
				if m == id {
					return true
				}
			}
			return false
		}
		isTgtAdmin := func() bool {
			for _, a := range g.Admins {
				if a == target {
					return true
				}
			}
			return false
		}
		notify = append([]uint64{}, g.Members...) // 变更前成员（含被踢/离开者）

		// 转移群主索引：oldOwner -gid, newOwner +gid
		reassignOwnerIndex := func(oldOwner, newOwner uint64) {
			oi := tx.Bucket(bkGroupOwner)
			var a, b []uint64
			getJSON(oi, itob(oldOwner), &a)
			a, _ = removeVal(a, gid)
			putJSON(oi, itob(oldOwner), a)
			getJSON(oi, itob(newOwner), &b)
			b, _ = addUnique(b, gid)
			putJSON(oi, itob(newOwner), b)
		}

		switch action {
		case "kick":
			if !isAdmin {
				return errors.New("无权限")
			}
			if target == g.OwnerID {
				return errors.New("不能踢出群主")
			}
			if !isMember(target) {
				return errors.New("对方不在组内")
			}
			if !isOwner && isTgtAdmin() {
				return errors.New("管理员不能踢出其他管理员")
			}
			g.Members, _ = removeVal(g.Members, target)
			g.Admins, _ = removeVal(g.Admins, target)
		case "grant":
			if !isOwner {
				return errors.New("仅群主可授予管理员")
			}
			if target == g.OwnerID || !isMember(target) {
				return errors.New("目标无效")
			}
			g.Admins, _ = addUnique(g.Admins, target)
		case "revoke":
			if !isOwner {
				return errors.New("仅群主可取消管理员")
			}
			g.Admins, _ = removeVal(g.Admins, target)
		case "transfer":
			if !isOwner {
				return errors.New("仅群主可转移群主")
			}
			if !isMember(target) {
				return errors.New("目标不在组内")
			}
			reassignOwnerIndex(g.OwnerID, target)
			g.OwnerID = target
			g.Admins, _ = removeVal(g.Admins, target)
		case "handover": // 转移群主并退出
			if !isOwner {
				return errors.New("仅群主可操作")
			}
			if !isMember(target) {
				return errors.New("目标不在组内")
			}
			reassignOwnerIndex(g.OwnerID, target)
			g.OwnerID = target
			g.Admins, _ = removeVal(g.Admins, target)
			g.Members, _ = removeVal(g.Members, actor)
			g.Admins, _ = removeVal(g.Admins, actor)
		case "disband":
			if !isOwner {
				return errors.New("仅群主可解散群")
			}
			disbanded = true
			if err := gb.Delete(itob(gid)); err != nil {
				return err
			}
			oi := tx.Bucket(bkGroupOwner)
			var owned []uint64
			getJSON(oi, itob(g.OwnerID), &owned)
			owned, _ = removeVal(owned, gid)
			putJSON(oi, itob(g.OwnerID), owned)
			return tx.Bucket(bkGroupReqs).Delete(itob(gid))
		default:
			return errors.New("未知操作")
		}
		return putJSON(gb, itob(gid), &g)
	})
	return g, notify, disbanded, err
}

// GetUserGroups 返回 userID 参与的所有组。
func (s *Store) GetUserGroups(userID uint64) []GroupRecord {
	var res []GroupRecord
	s.db.View(func(tx *bolt.Tx) error {
		return tx.Bucket(bkGroups).ForEach(func(k, v []byte) error {
			var g GroupRecord
			if json.Unmarshal(v, &g) != nil {
				return nil
			}
			for _, m := range g.Members {
				if m == userID {
					res = append(res, g)
					break
				}
			}
			return nil
		})
	})
	return res
}

// ---- 入群申请（离线保留）----

func (s *Store) AddGroupJoinRequest(req GroupJoinRequest) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkGroupReqs)
		var list []GroupJoinRequest
		getJSON(b, itob(req.GroupID), &list)
		out := list[:0:0]
		for _, r := range list {
			if r.ApplicantID != req.ApplicantID {
				out = append(out, r)
			}
		}
		out = append(out, req)
		return putJSON(b, itob(req.GroupID), out)
	})
}

// PeekGroupJoinRequestsForOwner 读取 ownerID 名下所有组的待处理入群申请（不清空）。
func (s *Store) PeekGroupJoinRequestsForOwner(ownerID uint64) []GroupJoinRequest {
	var res []GroupJoinRequest
	s.db.View(func(tx *bolt.Tx) error {
		oi := tx.Bucket(bkGroupOwner)
		var owned []uint64
		getJSON(oi, itob(ownerID), &owned)
		rb := tx.Bucket(bkGroupReqs)
		for _, gid := range owned {
			var list []GroupJoinRequest
			if getJSON(rb, itob(gid), &list) {
				res = append(res, list...)
			}
		}
		return nil
	})
	return res
}

func (s *Store) RemoveGroupJoinRequest(groupID, applicantID uint64) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkGroupReqs)
		var list []GroupJoinRequest
		getJSON(b, itob(groupID), &list)
		out := list[:0:0]
		for _, r := range list {
			if r.ApplicantID != applicantID {
				out = append(out, r)
			}
		}
		return putJSON(b, itob(groupID), out)
	})
}

// ===================== 网络授权 =====================

// ShareNetwork 判断两用户是否应处于同一虚拟局域网：互为好友 或 同属一个组。
func (s *Store) ShareNetwork(a, b uint64) bool {
	if a == b {
		return false
	}
	if s.AreFriends(a, b) {
		return true
	}
	share := false
	s.db.View(func(tx *bolt.Tx) error {
		tx.Bucket(bkGroups).ForEach(func(k, v []byte) error {
			var g GroupRecord
			if json.Unmarshal(v, &g) != nil {
				return nil
			}
			ina, inb := false, false
			for _, m := range g.Members {
				if m == a {
					ina = true
				}
				if m == b {
					inb = true
				}
			}
			if ina && inb {
				share = true
			}
			return nil
		})
		return nil
	})
	return share
}

// NetworkPeers 返回 userID 的所有网络对端（好友 ∪ 同组成员），去重、排除自己。
func (s *Store) NetworkPeers(userID uint64) []uint64 {
	set := map[uint64]bool{}
	for _, f := range s.GetFriends(userID) {
		set[f] = true
	}
	for _, g := range s.GetUserGroups(userID) {
		for _, m := range g.Members {
			if m != userID {
				set[m] = true
			}
		}
	}
	var out []uint64
	for id := range set {
		out = append(out, id)
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out
}

// ===================== 黑白名单 =====================

func (s *Store) GetAcl(userID uint64) AclRecord {
	acl := AclRecord{Mode: "black"}
	s.db.View(func(tx *bolt.Tx) error {
		getJSON(tx.Bucket(bkAcl), itob(userID), &acl)
		return nil
	})
	if acl.Mode == "" {
		acl.Mode = "black"
	}
	return acl
}

func (s *Store) SetAcl(userID uint64, acl AclRecord) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		return putJSON(tx.Bucket(bkAcl), itob(userID), &acl)
	})
}

// IsBlocked 从 owner 的视角判断 peer 是否被阻断（黑名单命中，或白名单模式下不在白名单）。
func (s *Store) IsBlocked(ownerID, peerID uint64) bool {
	acl := s.GetAcl(ownerID)
	if acl.Mode == "white" {
		for _, w := range acl.White {
			if w == peerID {
				return false
			}
		}
		return true
	}
	for _, bl := range acl.Black {
		if bl == peerID {
			return true
		}
	}
	return false
}

// ===================== 离线消息 =====================

func (s *Store) EnqueueOffline(userID uint64, msg OfflineMsg) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkOffline)
		var list []OfflineMsg
		getJSON(b, itob(userID), &list)
		list = append(list, msg)
		return putJSON(b, itob(userID), list)
	})
}

// PeekOffline 读取但不清空（等 CHAT_ACK 逐条出队）。
func (s *Store) PeekOffline(userID uint64) []OfflineMsg {
	var list []OfflineMsg
	s.db.View(func(tx *bolt.Tx) error {
		getJSON(tx.Bucket(bkOffline), itob(userID), &list)
		return nil
	})
	return list
}

// AckOffline 按 msgID 出队一条。
func (s *Store) AckOffline(userID uint64, msgID string) error {
	return s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket(bkOffline)
		var list []OfflineMsg
		getJSON(b, itob(userID), &list)
		out := list[:0:0]
		for _, m := range list {
			if m.MsgID != msgID {
				out = append(out, m)
			}
		}
		return putJSON(b, itob(userID), out)
	})
}
