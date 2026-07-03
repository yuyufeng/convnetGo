package main

// imserver.go —— IM/社交层服务端处理器。
// handleIM 从 handleServerConnection 的 switch default 分支进入，
// 处理好友/群组/在线态/聊天/ACL/中继等 opcode（>= FRIEND_SEARCH）。
// 所有推送都经 pushTo（按 userID 查在线连接）。

import (
	"net"
	"strings"
	"sync"
	"time"

	"github.com/labstack/gommon/log"
	"golang.org/x/crypto/bcrypt"
)

func nowUnix() int64 { return time.Now().Unix() }

// ---- 参数解析（JSON 数字到达时是 float64）----

func argStr(m []interface{}, i int) string {
	if i < len(m) {
		if s, ok := m[i].(string); ok {
			return s
		}
	}
	return ""
}

func argU64(m []interface{}, i int) uint64 {
	if i < len(m) {
		switch v := m[i].(type) {
		case float64:
			return uint64(v)
		case string:
			return uint64(Strtoint(v))
		}
	}
	return 0
}

func argBool(m []interface{}, i int) bool {
	if i < len(m) {
		if b, ok := m[i].(bool); ok {
			return b
		}
	}
	return false
}

func argU64List(m []interface{}, i int) []uint64 {
	var out []uint64
	if i < len(m) {
		if arr, ok := m[i].([]interface{}); ok {
			for _, e := range arr {
				switch v := e.(type) {
				case float64:
					out = append(out, uint64(v))
				case string:
					out = append(out, uint64(Strtoint(v)))
				}
			}
		}
	}
	return out
}

func u64sToIface(list []uint64) []interface{} {
	out := []interface{}{}
	for _, v := range list {
		out = append(out, v)
	}
	return out
}

func userIDFromPublicID(pid string) uint64 {
	parts := strings.Split(pid, ":")
	if len(parts) < 2 {
		return 0
	}
	return uint64(Strtoint(parts[len(parts)-1]))
}

// ---- 在线态 / 推送 ----

func publicIDForUser(userID uint64) (string, bool) {
	if store == nil {
		return "", false
	}
	u, ok := store.GetUser(userID)
	return u.PublicID, ok
}

func isOnline(userID uint64) bool {
	pid, ok := publicIDForUser(userID)
	if !ok {
		return false
	}
	_, online := serverConnMap.Load(pid)
	return online
}

// pushTo 向指定 userID 的在线连接推送一条消息；离线返回 false。
func pushTo(userID uint64, cmd int, msg []interface{}) bool {
	pid, ok := publicIDForUser(userID)
	if !ok {
		return false
	}
	v, ok := serverConnMap.Load(pid)
	if !ok {
		return false
	}
	sendToConn(*v.(serverConn).Conn, cmd, msg)
	return true
}

// ---- 花名册构造 ----

// userNicMode 记录在线用户上报的网卡模式（userID -> "tun"/"tap"）。仅内存、按会话；
// 纯本地模式，服务器不强制，只随花名册/在线态转发给对端做“是否一致”提示。
var userNicMode sync.Map

func nicModeOf(userID uint64) string {
	if v, ok := userNicMode.Load(userID); ok {
		if s, ok2 := v.(string); ok2 && s != "" {
			return s
		}
	}
	return "tun" // 默认
}

func rosterEntry(userID uint64) map[string]interface{} {
	u, _ := store.GetUser(userID)
	return map[string]interface{}{
		"UserID":   userID,
		"PublicID": u.PublicID,
		"Nick":     u.Nick,
		"CvnIP":    u.CvnIP,
		"Mac":      u.Mac,
		"Online":   isOnline(userID),
		"NicMode":  nicModeOf(userID),
	}
}

func groupSummary(g GroupRecord) map[string]interface{} {
	return map[string]interface{}{
		"GroupID":     g.GroupID,
		"Name":        g.Name,
		"OwnerID":     g.OwnerID,
		"MemberCount": len(g.Members),
		"HasPassword": g.JoinPassword != "", // 客户端据此决定是否提示输入入群密码
	}
}

func groupDetail(g GroupRecord) map[string]interface{} {
	members := []interface{}{}
	for _, m := range g.Members {
		members = append(members, rosterEntry(m))
	}
	return map[string]interface{}{
		"GroupID": g.GroupID,
		"Name":    g.Name,
		"OwnerID": g.OwnerID,
		"Admins":  u64sToIface(g.Admins),
		"Members": members,
	}
}

func opResp(conn net.Conn, ok bool, message string, payload interface{}) {
	sendToConn(conn, GROUP_OP_RESP, []interface{}{ok, message, payload})
}

// ===================== 登录/断开钩子 =====================

// persistIdentityOnRegister 持久化用户身份。必须在发送 WS_REGISTE_RESP 之前调用，
// 保证客户端得知"已注册"时其身份记录已落库（否则紧接着的搜索/加好友会查不到）。
func persistIdentityOnRegister(cvClient *peerClient) {
	if store == nil || cvClient.PublicID == "" {
		return
	}
	userID := userIDFromPublicID(cvClient.PublicID)
	nick, mac := "", ""
	if cvClient.MainClientInfo != nil {
		nick, mac = cvClient.MainClientInfo.Name, cvClient.MainClientInfo.Mac
	}
	if err := store.UpsertUser(userID, cvClient.PublicID, nick, mac); err != nil {
		log.Error("UpsertUser 失败:", err)
	}
}

// onClientRegistered 在 WS_REGISTE_RESP 发送之后调用：上线广播 + 下发离线申请/消息。
func onClientRegistered(cvClient *peerClient) {
	if store == nil || cvClient.PublicID == "" {
		return
	}
	userID := userIDFromPublicID(cvClient.PublicID)

	// 向所有网络对端（好友∪同组成员）广播上线（附网卡模式，供对端判断是否一致）
	for _, p := range store.NetworkPeers(userID) {
		pushTo(p, PRESENCE_NOTIFY, []interface{}{userID, true, nicModeOf(userID)})
	}

	// 下发待处理的好友申请（不清空，接受/拒绝时才移除）
	for _, r := range store.PeekFriendRequests(userID) {
		fromRec, _ := store.GetUser(r.FromUserID)
		pushTo(userID, FRIEND_REQUEST_PUSH,
			[]interface{}{r.FromUserID, fromRec.PublicID, r.FromNick, r.Greeting})
	}
	// 下发待处理的入群申请（作为群主）
	for _, r := range store.PeekGroupJoinRequestsForOwner(userID) {
		pushTo(userID, GROUP_JOIN_PUSH,
			[]interface{}{r.GroupID, r.ApplicantID, r.ApplicantNick})
	}
	// 下发离线聊天消息（不清空，等 CHAT_ACK 逐条出队）
	for _, m := range store.PeekOffline(userID) {
		pushTo(userID, CHAT_DELIVER,
			[]interface{}{m.Scope, m.FromUserID, m.FromNick, m.GroupID, m.MsgID, m.ContentType, m.RichText, m.Ts})
	}
}

// handleAccountAuth 处理账号注册/登录。成功后绑定连接（存 serverConnMap）、
// 回 WS_REGISTE_RESP[PublicID,nick]；失败回 WS_REGISTE_FAIL[reason]。
// 该函数在 shServer 的 switch 中于鉴权前调用（此时 cvClient.PublicID 尚为空）。
func handleAccountAuth(cvClient *peerClient, isRegister bool, msg []interface{}) {
	if store == nil {
		return
	}
	conn := *cvClient.Conn
	account := strings.TrimSpace(argStr(msg, 0))
	password := argStr(msg, 1)
	var nick, mac string
	if isRegister {
		nick = strings.TrimSpace(argStr(msg, 2))
		mac = argStr(msg, 3)
	} else {
		mac = argStr(msg, 2)
	}
	if account == "" || password == "" {
		sendToConn(conn, WS_REGISTE_FAIL, []interface{}{"账号和密码不能为空"})
		return
	}

	var rec UserRecord
	if isRegister {
		if nick == "" {
			nick = account
		}
		hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
		if err != nil {
			sendToConn(conn, WS_REGISTE_FAIL, []interface{}{"服务器内部错误"})
			return
		}
		r, err := store.CreateAccount(account, string(hash), nick)
		if err != nil {
			sendToConn(conn, WS_REGISTE_FAIL, []interface{}{err.Error()})
			return
		}
		rec = r
	} else {
		r, ok := store.GetUserByAccount(account)
		if !ok || bcrypt.CompareHashAndPassword([]byte(r.PassHash), []byte(password)) != nil {
			sendToConn(conn, WS_REGISTE_FAIL, []interface{}{"账号或密码错误"})
			return
		}
		rec = r
	}

	// 同一账号不允许重复登录
	if _, online := serverConnMap.Load(rec.PublicID); online {
		sendToConn(conn, WS_REGISTE_FAIL, []interface{}{"该账号已在别处登录"})
		return
	}

	// 绑定连接（等价于旧 WS_REGISTE 的登记逻辑）
	info := &mainClientInfo{
		CanNat:     true,
		Version:    "1.0",
		Name:       rec.Nick,
		PublicID:   rec.PublicID,
		ClientMode: float64(CLIENT),
		Mac:        mac,
		IP:         rec.CvnIP,
	}
	cvClient.PublicID = rec.PublicID
	cvClient.MainClientInfo = info
	serverConnMap.Store(rec.PublicID, serverConn{Conn: cvClient.Conn, MainClientInfo: info})

	persistIdentityOnRegister(cvClient) // 刷新 mac（UpsertUser 保留 Account/PassHash）
	sendToConn(conn, WS_REGISTE_RESP, []interface{}{rec.PublicID, rec.Nick, rec.CvnIP})
	onClientRegistered(cvClient)
}

// onClientDisconnected 在 serverConnMap.Delete 之后调用。
func onClientDisconnected(publicID string) {
	if store == nil || publicID == "" {
		return
	}
	userID := userIDFromPublicID(publicID)
	for _, p := range store.NetworkPeers(userID) {
		pushTo(p, PRESENCE_NOTIFY, []interface{}{userID, false, nicModeOf(userID)})
	}
	userNicMode.Delete(userID) // 会话结束清理，下次登录重新上报
}

// ===================== 主分发 =====================

// handleIM 处理 IM 层 opcode。cmd < FRIEND_SEARCH 的走原有逻辑，不进这里。
func handleIM(cvClient *peerClient, cmd int, msg []interface{}) {
	if store == nil {
		return
	}
	conn := *cvClient.Conn
	if cvClient.PublicID == "" {
		// 未注册禁止 IM 操作
		opResp(conn, false, "请先登录", nil)
		return
	}
	self := userIDFromPublicID(cvClient.PublicID)

	switch cmd {
	// ---------- 好友 ----------
	case FRIEND_SEARCH:
		entries := []interface{}{}
		for _, u := range store.SearchUsers(argStr(msg, 0), self) {
			entries = append(entries, map[string]interface{}{
				"UserID": u.UserID, "PublicID": u.PublicID, "Nick": u.Nick, "Online": isOnline(u.UserID),
			})
		}
		sendToConn(conn, FRIEND_SEARCH_RESP, []interface{}{entries})

	case FRIEND_REQUEST:
		to := argU64(msg, 0)
		greeting := argStr(msg, 1)
		if to == 0 || to == self {
			opResp(conn, false, "无效的目标用户", nil)
			return
		}
		if store.AreFriends(self, to) {
			opResp(conn, false, "已经是好友", nil)
			return
		}
		selfRec, _ := store.GetUser(self)
		store.AddFriendRequest(to, FriendRequest{
			FromUserID: self, FromNick: selfRec.Nick, Greeting: greeting, Ts: nowUnix(),
		})
		pushTo(to, FRIEND_REQUEST_PUSH, []interface{}{self, selfRec.PublicID, selfRec.Nick, greeting})
		opResp(conn, true, "好友申请已发送", nil)

	case FRIEND_ACCEPT:
		from := argU64(msg, 0)
		accept := argBool(msg, 1)
		store.RemoveFriendRequest(self, from)
		if accept {
			if err := store.AddFriendPair(self, from); err != nil {
				opResp(conn, false, err.Error(), nil)
				return
			}
			// 双向回推对方花名册 —— 触发两端组网
			pushTo(from, FRIEND_ACCEPT_PUSH, []interface{}{rosterEntry(self), true})
			pushTo(self, FRIEND_ACCEPT_PUSH, []interface{}{rosterEntry(from), true})
		} else {
			pushTo(from, FRIEND_ACCEPT_PUSH, []interface{}{rosterEntry(self), false})
		}

	case FRIEND_LIST:
		entries := []interface{}{}
		for _, f := range store.GetFriends(self) {
			entries = append(entries, rosterEntry(f))
		}
		sendToConn(conn, FRIEND_LIST_RESP, []interface{}{entries})

	case FRIEND_REMOVE:
		peer := argU64(msg, 0)
		store.RemoveFriendPair(self, peer)
		pushTo(peer, FRIEND_REMOVE_PUSH, []interface{}{self})
		opResp(conn, true, "已删除好友", nil)

	// ---------- 用户组 ----------
	case GROUP_CREATE:
		name := strings.TrimSpace(argStr(msg, 0))
		password := argStr(msg, 1) // 可选：入群密码（设了就免审批）
		if name == "" {
			opResp(conn, false, "组名不能为空", nil)
			return
		}
		passHash := ""
		if password != "" {
			h, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
			if err != nil {
				opResp(conn, false, "服务器内部错误", nil)
				return
			}
			passHash = string(h)
		}
		g, err := store.CreateGroup(self, name, passHash)
		if err != nil {
			opResp(conn, false, err.Error(), nil)
			return
		}
		opResp(conn, true, "创建成功", groupDetail(g))

	case GROUP_SEARCH:
		entries := []interface{}{}
		for _, g := range store.SearchGroups(argStr(msg, 0)) {
			entries = append(entries, groupSummary(g))
		}
		sendToConn(conn, GROUP_SEARCH_RESP, []interface{}{entries})

	case GROUP_JOIN_REQUEST:
		gid := argU64(msg, 0)
		password := argStr(msg, 1) // 可选：入群密码
		g, ok := store.GetGroup(gid)
		if !ok {
			opResp(conn, false, "组不存在", nil)
			return
		}
		for _, m := range g.Members {
			if m == self {
				opResp(conn, false, "已在组内", nil)
				return
			}
		}
		if g.JoinPassword != "" {
			// 密码入组：校验通过即直接加入，无需群主审批
			if bcrypt.CompareHashAndPassword([]byte(g.JoinPassword), []byte(password)) != nil {
				opResp(conn, false, "入群密码错误", nil)
				return
			}
			ng, err := store.AddGroupMember(gid, self)
			if err != nil {
				opResp(conn, false, err.Error(), nil)
				return
			}
			roster := []interface{}{}
			for _, m := range ng.Members {
				roster = append(roster, rosterEntry(m))
			}
			sendToConn(conn, GROUP_JOIN_RESULT_PUSH, []interface{}{gid, ng.Name, true, roster})
			for _, m := range ng.Members {
				if m != self {
					pushTo(m, GROUP_MEMBER_CHANGE_PUSH, []interface{}{gid, true, rosterEntry(self)})
				}
			}
			return
		}
		// 无密码：走群主审批流程
		selfRec, _ := store.GetUser(self)
		store.AddGroupJoinRequest(GroupJoinRequest{
			GroupID: gid, ApplicantID: self, ApplicantNick: selfRec.Nick, Ts: nowUnix(),
		})
		pushTo(g.OwnerID, GROUP_JOIN_PUSH, []interface{}{gid, self, selfRec.Nick})
		opResp(conn, true, "入群申请已发送", nil)

	case GROUP_JOIN_APPROVE:
		gid := argU64(msg, 0)
		applicant := argU64(msg, 1)
		approve := argBool(msg, 2)
		g, ok := store.GetGroup(gid)
		if !ok || g.OwnerID != self {
			opResp(conn, false, "无权限或组不存在", nil)
			return
		}
		store.RemoveGroupJoinRequest(gid, applicant)
		if !approve {
			pushTo(applicant, GROUP_JOIN_RESULT_PUSH, []interface{}{gid, g.Name, false, []interface{}{}})
			opResp(conn, true, "已拒绝", nil)
			return
		}
		ng, err := store.AddGroupMember(gid, applicant)
		if err != nil {
			opResp(conn, false, err.Error(), nil)
			pushTo(applicant, GROUP_JOIN_RESULT_PUSH, []interface{}{gid, g.Name, false, []interface{}{}})
			return
		}
		// 给申请人下发完整花名册 —— 触发其与所有组员组网
		roster := []interface{}{}
		for _, m := range ng.Members {
			roster = append(roster, rosterEntry(m))
		}
		pushTo(applicant, GROUP_JOIN_RESULT_PUSH, []interface{}{gid, ng.Name, true, roster})
		// 通知既有组员：新成员加入
		for _, m := range ng.Members {
			if m != applicant {
				pushTo(m, GROUP_MEMBER_CHANGE_PUSH, []interface{}{gid, true, rosterEntry(applicant)})
			}
		}
		opResp(conn, true, "已通过", nil)

	case GROUP_MANAGE:
		gid := argU64(msg, 0)
		action := argStr(msg, 1)
		target := argU64(msg, 2)
		_, notify, disbanded, err := store.GroupManage(gid, self, target, action)
		if err != nil {
			opResp(conn, false, err.Error(), nil)
			return
		}
		for _, m := range notify {
			pushTo(m, GROUP_MEMBER_CHANGE_PUSH, []interface{}{gid, !disbanded, target})
		}
		labels := map[string]string{
			"kick": "已踢出成员", "grant": "已设为管理员", "revoke": "已取消管理员",
			"transfer": "已转移群主", "handover": "已转移群主并退出", "disband": "已解散群",
		}
		opResp(conn, true, labels[action], nil)

	case GROUP_LEAVE:
		gid := argU64(msg, 0)
		if g, ok := store.GetGroup(gid); ok && g.OwnerID == self && len(g.Members) > 1 {
			opResp(conn, false, "群主退出前需先转移群主，或解散该群", nil)
			return
		}
		_, affected, disbanded, err := store.RemoveGroupMember(gid, self)
		if err != nil {
			opResp(conn, false, err.Error(), nil)
			return
		}
		for _, m := range affected {
			if m != self {
				pushTo(m, GROUP_MEMBER_CHANGE_PUSH, []interface{}{gid, false, rosterEntry(self)})
			}
		}
		if disbanded {
			opResp(conn, true, "已解散该组", nil)
		} else {
			opResp(conn, true, "已退出该组", nil)
		}

	case GROUP_LIST:
		entries := []interface{}{}
		for _, g := range store.GetUserGroups(self) {
			entries = append(entries, groupDetail(g))
		}
		sendToConn(conn, GROUP_LIST_RESP, []interface{}{entries})

	case PRESENCE_SUBSCRIBE:
		// M1：在线态自动推送，无需订阅，忽略。

	case NIC_MODE_REPORT:
		// 客户端上报本机网卡模式；登记并转发给所有网络对端（借 PRESENCE_NOTIFY 携带模式）
		mode := argStr(msg, 0)
		if mode != "tap" {
			mode = "tun"
		}
		userNicMode.Store(self, mode)
		for _, p := range store.NetworkPeers(self) {
			pushTo(p, PRESENCE_NOTIFY, []interface{}{self, true, mode})
		}

	// ---------- 聊天（存储转发）----------
	case CHAT_SEND:
		handleChatSend(cvClient, self, conn, msg)

	case CHAT_ACK:
		store.AckOffline(self, argStr(msg, 0))

	case CHAT_HISTORY_REQ:
		// M1：暂不实现历史漫游，返回空。
		sendToConn(conn, CHAT_HISTORY_RESP, []interface{}{[]interface{}{}})

	// ---------- 黑白名单 ----------
	case ACL_SET:
		acl := AclRecord{
			Mode:  argStr(msg, 0),
			Black: argU64List(msg, 1),
			White: argU64List(msg, 2),
		}
		if acl.Mode != "white" {
			acl.Mode = "black"
		}
		store.SetAcl(self, acl)
		opResp(conn, true, "黑白名单已更新", nil)

	case ACL_GET:
		acl := store.GetAcl(self)
		sendToConn(conn, ACL_GET_RESP,
			[]interface{}{acl.Mode, u64sToIface(acl.Black), u64sToIface(acl.White)})

	// ---------- P2P 失败回退：盲中继 ----------
	case RELAY_DATA:
		targetPID := argStr(msg, 0)
		blob := argStr(msg, 1)
		target := userIDFromPublicID(targetPID)
		if target == 0 || !store.ShareNetwork(self, target) {
			return
		}
		if store.IsBlocked(target, self) || store.IsBlocked(self, target) {
			return
		}
		// 服务器中继准入 + 限速（由管理后台配置）
		if relayCtl != nil && !relayCtl.Allow(self, len(blob)) {
			return
		}
		if v, ok := serverConnMap.Load(targetPID); ok {
			sendToConn(*v.(serverConn).Conn, RELAY_DATA_FWD, []interface{}{cvClient.PublicID, blob})
		}

	case RELAY_OPEN, RELAY_CLOSE:
		// M1：中继是无状态逐包转发，OPEN/CLOSE 仅作占位，忽略。

	default:
		log.Debug("未知 IM opcode:", cmd)
	}
}

func handleChatSend(cvClient *peerClient, self uint64, conn net.Conn, msg []interface{}) {
	scope := argStr(msg, 0)
	target := argU64(msg, 1)
	msgID := argStr(msg, 2)
	ctype := argStr(msg, 3)
	rich := argStr(msg, 4)
	selfRec, _ := store.GetUser(self)
	ts := nowUnix()

	switch scope {
	case "u":
		if !store.ShareNetwork(self, target) {
			opResp(conn, false, "非好友/同组，不能发送", msgID)
			return
		}
		deliver := []interface{}{"u", self, selfRec.Nick, uint64(0), msgID, ctype, rich, ts}
		if !pushTo(target, CHAT_DELIVER, deliver) {
			store.EnqueueOffline(target, OfflineMsg{
				Scope: "u", FromUserID: self, FromNick: selfRec.Nick,
				MsgID: msgID, ContentType: ctype, RichText: rich, Ts: ts,
			})
		}
		sendToConn(conn, CHAT_ACK, []interface{}{msgID})

	case "g":
		g, ok := store.GetGroup(target)
		if !ok {
			opResp(conn, false, "组不存在", msgID)
			return
		}
		member := false
		for _, m := range g.Members {
			if m == self {
				member = true
				break
			}
		}
		if !member {
			opResp(conn, false, "不在组内", msgID)
			return
		}
		deliver := []interface{}{"g", self, selfRec.Nick, target, msgID, ctype, rich, ts}
		for _, m := range g.Members {
			if m == self {
				continue
			}
			if !pushTo(m, CHAT_DELIVER, deliver) {
				// 群离线消息 msgID 附收件人后缀，便于各自 ACK 出队
				store.EnqueueOffline(m, OfflineMsg{
					Scope: "g", FromUserID: self, FromNick: selfRec.Nick, GroupID: target,
					MsgID: msgID + ":" + Inttostr(int(m)), ContentType: ctype, RichText: rich, Ts: ts,
				})
			}
		}
		sendToConn(conn, CHAT_ACK, []interface{}{msgID})

	default:
		opResp(conn, false, "未知消息范围", msgID)
	}
}
