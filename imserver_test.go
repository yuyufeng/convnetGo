package main

// imserver_test.go —— M1 端到端测试：用 net.Pipe 直连 handleServerConnection，
// 两/三个假客户端跑通 注册->搜索->加好友->聊天->建群(<=5)->入群(<=10)->离线消息。

import (
	"bufio"
	"encoding/json"
	"io"
	"net"
	"path/filepath"
	"testing"
	"time"
)

// ---- 假客户端：一端接 handleServerConnection，另一端由测试驱动 ----

type fakeClient struct {
	conn    net.Conn
	in      chan clientMessage
	backlog []clientMessage
}

func newFakeClient() *fakeClient {
	serverSide, clientSide := net.Pipe()
	pc := peerClient{Conn: &serverSide}
	go handleServerConnection(pc)
	fc := &fakeClient{conn: clientSide, in: make(chan clientMessage, 128)}
	go fc.readLoop()
	return fc
}

func (fc *fakeClient) readLoop() {
	reader := bufio.NewReader(fc.conn)
	for {
		lenBytes := make([]byte, ConstSaveDataLength)
		if _, err := io.ReadFull(reader, lenBytes); err != nil {
			close(fc.in)
			return
		}
		n := BytesToInt(lenBytes)
		buf := make([]byte, n)
		if _, err := io.ReadFull(reader, buf); err != nil {
			close(fc.in)
			return
		}
		var cm clientMessage
		if json.Unmarshal(buf, &cm) == nil {
			fc.in <- cm
		}
	}
}

func (fc *fakeClient) send(cmd int, msg []interface{}) { sendToConn(fc.conn, cmd, msg) }

func (fc *fakeClient) close() { fc.conn.Close() }

// wait 阻塞等待指定 opcode，其余消息暂存 backlog，供后续 wait 命中。
func (fc *fakeClient) wait(t *testing.T, cmd int) clientMessage {
	t.Helper()
	for i, cm := range fc.backlog {
		if cm.CMDType == cmd {
			fc.backlog = append(fc.backlog[:i], fc.backlog[i+1:]...)
			return cm
		}
	}
	deadline := time.After(2 * time.Second)
	for {
		select {
		case cm, ok := <-fc.in:
			if !ok {
				t.Fatalf("连接已关闭，仍在等待 opcode %d", cmd)
			}
			if cm.CMDType == cmd {
				return cm
			}
			fc.backlog = append(fc.backlog, cm)
		case <-deadline:
			t.Fatalf("等待 opcode %d 超时", cmd)
		}
	}
}

func (fc *fakeClient) register(t *testing.T, uuid, nick, mac string) string {
	t.Helper()
	fc.send(WS_REGISTE, []interface{}{"1.0", uuid, nick, float64(CLIENT), mac, "",
		[]interface{}{}, []interface{}{}})
	resp := fc.wait(t, WS_REGISTE_RESP)
	return resp.Message[0].(string)
}

// registerAccount 账号注册，成功返回 PublicID。
func (fc *fakeClient) registerAccount(t *testing.T, account, password, nick, mac string) string {
	t.Helper()
	fc.send(ACCOUNT_REGISTER, []interface{}{account, password, nick, mac})
	resp := fc.wait(t, WS_REGISTE_RESP)
	return resp.Message[0].(string)
}

func asF64(v interface{}) float64 { f, _ := v.(float64); return f }
func asBool(v interface{}) bool   { b, _ := v.(bool); return b }
func asStr(v interface{}) string  { s, _ := v.(string); return s }

// resetServerState 清理跨测试污染的全局状态（serverConnMap/uuidToUserID/计数器）。
func resetServerState() {
	serverConnMap.Range(func(k, _ interface{}) bool { serverConnMap.Delete(k); return true })
	uuidToUserID.Range(func(k, _ interface{}) bool { uuidToUserID.Delete(k); return true })
	userIDCounter = 0
}

func waitUntilOffline(t *testing.T, pid string) {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if _, ok := serverConnMap.Load(pid); !ok {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatalf("客户端 %s 仍在线", pid)
}

// ---- 主测试 ----

func TestIM_M1(t *testing.T) {
	// 隔离全局状态与落盘路径
	dir := t.TempDir()
	uuidFilePath = filepath.Join(dir, "uuid.json")
	resetServerState()
	if err := StoreInit(filepath.Join(dir, "convnet.db")); err != nil {
		t.Fatalf("StoreInit: %v", err)
	}
	defer store.db.Close()

	alice := newFakeClient()
	bob := newFakeClient()
	pidA := alice.register(t, "uuid-A", "alice", "aa:aa:aa:aa:aa:aa")
	pidB := bob.register(t, "uuid-B", "bob", "bb:bb:bb:bb:bb:bb")
	t.Logf("pidA=%s pidB=%s", pidA, pidB)
	if userIDFromPublicID(pidA) != 1 || userIDFromPublicID(pidB) != 2 {
		t.Fatalf("期望 userID 1/2，得到 %d/%d", userIDFromPublicID(pidA), userIDFromPublicID(pidB))
	}

	// --- 好友搜索 ---
	alice.send(FRIEND_SEARCH, []interface{}{"bob"})
	sr := alice.wait(t, FRIEND_SEARCH_RESP)
	entries := sr.Message[0].([]interface{})
	if len(entries) != 1 || asF64(entries[0].(map[string]interface{})["UserID"]) != 2 {
		t.Fatalf("搜索 bob 失败: %v", entries)
	}

	// --- 好友申请 / 接受 ---
	alice.send(FRIEND_REQUEST, []interface{}{float64(2), "hi bob"})
	if !asBool(alice.wait(t, GROUP_OP_RESP).Message[0]) {
		t.Fatal("好友申请应成功")
	}
	fp := bob.wait(t, FRIEND_REQUEST_PUSH)
	if asF64(fp.Message[0]) != 1 || asStr(fp.Message[2]) != "alice" {
		t.Fatalf("好友申请推送内容错误: %v", fp.Message)
	}
	bob.send(FRIEND_ACCEPT, []interface{}{float64(1), true})
	// 双方各收到对方花名册
	ap := alice.wait(t, FRIEND_ACCEPT_PUSH)
	if asF64(ap.Message[0].(map[string]interface{})["UserID"]) != 2 || !asBool(ap.Message[1]) {
		t.Fatalf("alice 未正确收到 bob 花名册: %v", ap.Message)
	}
	bp := bob.wait(t, FRIEND_ACCEPT_PUSH)
	if asF64(bp.Message[0].(map[string]interface{})["UserID"]) != 1 {
		t.Fatalf("bob 未正确收到 alice 花名册: %v", bp.Message)
	}

	// --- 好友列表 ---
	alice.send(FRIEND_LIST, []interface{}{})
	fl := alice.wait(t, FRIEND_LIST_RESP).Message[0].([]interface{})
	if len(fl) != 1 || asF64(fl[0].(map[string]interface{})["UserID"]) != 2 {
		t.Fatalf("好友列表错误: %v", fl)
	}

	// --- 单聊富文本 ---
	alice.send(CHAT_SEND, []interface{}{"u", float64(2), "m1", "rich", "<b>hello</b>😀"})
	if asStr(alice.wait(t, CHAT_ACK).Message[0]) != "m1" {
		t.Fatal("发送方应收到 CHAT_ACK[m1]")
	}
	dl := bob.wait(t, CHAT_DELIVER)
	if asStr(dl.Message[0]) != "u" || asStr(dl.Message[6]) != "<b>hello</b>😀" {
		t.Fatalf("bob 未收到正确富文本: %v", dl.Message)
	}

	// --- 建群上限 <=5（群主 alice）---
	for i := 0; i < 5; i++ {
		alice.send(GROUP_CREATE, []interface{}{"g" + Inttostr(i)})
		if !asBool(alice.wait(t, GROUP_OP_RESP).Message[0]) {
			t.Fatalf("第 %d 个群应创建成功", i+1)
		}
	}
	alice.send(GROUP_CREATE, []interface{}{"g6"})
	r6 := alice.wait(t, GROUP_OP_RESP)
	if asBool(r6.Message[0]) || asStr(r6.Message[1]) != "最多创建5个组" {
		t.Fatalf("第 6 个群应被拒绝: %v", r6.Message)
	}

	// --- 入群成功（群主 bob，未满）---
	gx, err := store.CreateGroup(2, "groupX", "")
	if err != nil {
		t.Fatalf("bob 建 groupX: %v", err)
	}
	carol := newFakeClient()
	pidC := carol.register(t, "uuid-C", "carol", "cc:cc:cc:cc:cc:cc")
	_ = pidC
	carol.send(GROUP_JOIN_REQUEST, []interface{}{float64(gx.GroupID)})
	if !asBool(carol.wait(t, GROUP_OP_RESP).Message[0]) {
		t.Fatal("入群申请应成功发送")
	}
	jp := bob.wait(t, GROUP_JOIN_PUSH) // 群主收到申请
	if asF64(jp.Message[0]) != float64(gx.GroupID) || asF64(jp.Message[1]) != 3 {
		t.Fatalf("群主收到的入群申请错误: %v", jp.Message)
	}
	bob.send(GROUP_JOIN_APPROVE, []interface{}{float64(gx.GroupID), float64(3), true})
	if !asBool(bob.wait(t, GROUP_OP_RESP).Message[0]) {
		t.Fatal("审批应成功")
	}
	rp := carol.wait(t, GROUP_JOIN_RESULT_PUSH)
	if !asBool(rp.Message[2]) || len(rp.Message[3].([]interface{})) != 2 {
		t.Fatalf("carol 应入群成功且花名册含2人: %v", rp.Message)
	}

	// --- 入群上限 <=10（群主 bob，已满）---
	gy, _ := store.CreateGroup(2, "groupY", "")
	for uid := uint64(101); uid <= 109; uid++ { // 补到 10 人（含 bob）
		if _, e := store.AddGroupMember(gy.GroupID, uid); e != nil {
			t.Fatalf("填充 groupY 失败: %v", e)
		}
	}
	carol.send(GROUP_JOIN_REQUEST, []interface{}{float64(gy.GroupID)})
	carol.wait(t, GROUP_OP_RESP)
	bob.wait(t, GROUP_JOIN_PUSH)
	bob.send(GROUP_JOIN_APPROVE, []interface{}{float64(gy.GroupID), float64(3), true})
	rfull := bob.wait(t, GROUP_OP_RESP)
	if asBool(rfull.Message[0]) || asStr(rfull.Message[1]) != "该组已满（最多10人）" {
		t.Fatalf("满员群审批应被拒: %v", rfull.Message)
	}
	rrej := carol.wait(t, GROUP_JOIN_RESULT_PUSH)
	if asBool(rrej.Message[2]) {
		t.Fatalf("carol 应入群失败: %v", rrej.Message)
	}

	// --- 离线消息：关闭 bob，alice 发消息，bob 重连后收到 ---
	bob.close()
	waitUntilOffline(t, pidB)
	alice.send(CHAT_SEND, []interface{}{"u", float64(2), "m2", "rich", "离线你好"})
	if asStr(alice.wait(t, CHAT_ACK).Message[0]) != "m2" {
		t.Fatal("离线发送也应收到 CHAT_ACK")
	}
	bob2 := newFakeClient()
	pidB2 := bob2.register(t, "uuid-B", "bob", "bb:bb:bb:bb:bb:bb")
	if pidB2 != pidB {
		t.Fatalf("重连后 PublicID 应一致: %s != %s", pidB2, pidB)
	}
	off := bob2.wait(t, CHAT_DELIVER)
	if asStr(off.Message[4]) != "m2" || asStr(off.Message[6]) != "离线你好" {
		t.Fatalf("重连后未收到离线消息: %v", off.Message)
	}
	// 确认 ACK 出队
	bob2.send(CHAT_ACK, []interface{}{"m2"})
	time.Sleep(50 * time.Millisecond)
	if len(store.PeekOffline(2)) != 0 {
		t.Fatalf("离线队列未清空: %v", store.PeekOffline(2))
	}

	t.Log("M1 端到端全部通过")
}

// ---- 账号密码登录测试 ----

func TestIM_Account(t *testing.T) {
	dir := t.TempDir()
	uuidFilePath = filepath.Join(dir, "uuid.json")
	resetServerState()
	if err := StoreInit(filepath.Join(dir, "convnet_acct.db")); err != nil {
		t.Fatalf("StoreInit: %v", err)
	}
	defer store.db.Close()

	alice := newFakeClient()
	bob := newFakeClient()
	pidA := alice.registerAccount(t, "alice", "pw-alice", "Alice", "aa:aa:aa:aa:aa:aa")
	pidB := bob.registerAccount(t, "bob", "pw-bob", "Bob", "bb:bb:bb:bb:bb:bb")
	if userIDFromPublicID(pidA) != 1 || userIDFromPublicID(pidB) != 2 {
		t.Fatalf("账号 userID 期望 1/2，得到 %d/%d", userIDFromPublicID(pidA), userIDFromPublicID(pidB))
	}

	// 重名注册应被拒
	dup := newFakeClient()
	dup.send(ACCOUNT_REGISTER, []interface{}{"alice", "x", "Alice2", ""})
	if msg := dup.wait(t, WS_REGISTE_FAIL); asStr(msg.Message[0]) != "账号已存在" {
		t.Fatalf("重名注册应返回“账号已存在”，得到: %v", msg.Message)
	}

	// 账号登录后可正常加好友 + 单聊
	alice.send(FRIEND_REQUEST, []interface{}{float64(2), "hi"})
	alice.wait(t, GROUP_OP_RESP)
	bob.wait(t, FRIEND_REQUEST_PUSH)
	bob.send(FRIEND_ACCEPT, []interface{}{float64(1), true})
	alice.wait(t, FRIEND_ACCEPT_PUSH)
	bob.wait(t, FRIEND_ACCEPT_PUSH)

	// bob 下线，alice 发离线消息
	bob.close()
	waitUntilOffline(t, pidB)
	alice.send(CHAT_SEND, []interface{}{"u", float64(2), "m-off", "rich", "账号离线消息"})
	alice.wait(t, CHAT_ACK)

	// 错误密码登录应被拒
	badLogin := newFakeClient()
	badLogin.send(ACCOUNT_LOGIN, []interface{}{"bob", "wrong-pw", "bb:bb:bb:bb:bb:bb"})
	if msg := badLogin.wait(t, WS_REGISTE_FAIL); asStr(msg.Message[0]) != "账号或密码错误" {
		t.Fatalf("错误密码应返回“账号或密码错误”，得到: %v", msg.Message)
	}

	// 正确密码登录成功，并收到离线消息
	bob2 := newFakeClient()
	pidB2 := bob2.registerLogin(t, "bob", "pw-bob", "bb:bb:bb:bb:bb:bb")
	if pidB2 != pidB {
		t.Fatalf("重新登录 PublicID 应一致: %s != %s", pidB2, pidB)
	}
	off := bob2.wait(t, CHAT_DELIVER)
	if asStr(off.Message[4]) != "m-off" || asStr(off.Message[6]) != "账号离线消息" {
		t.Fatalf("登录后未收到离线消息: %v", off.Message)
	}

	t.Log("账号密码登录端到端通过")
}

// 入群密码：凭密码免审批直接加入
func TestIM_GroupPassword(t *testing.T) {
	dir := t.TempDir()
	uuidFilePath = filepath.Join(dir, "uuid.json")
	resetServerState()
	if err := StoreInit(filepath.Join(dir, "convnet_gp.db")); err != nil {
		t.Fatalf("StoreInit: %v", err)
	}
	defer store.db.Close()

	alice := newFakeClient()
	bob := newFakeClient()
	alice.registerAccount(t, "alice", "pw", "Alice", "")
	bob.registerAccount(t, "bob", "pw", "Bob", "")

	// 建带入群密码的群
	alice.send(GROUP_CREATE, []interface{}{"pwgroup", "secret"})
	resp := alice.wait(t, GROUP_OP_RESP)
	if !asBool(resp.Message[0]) {
		t.Fatalf("建群应成功: %v", resp.Message)
	}
	payload := resp.Message[2].(map[string]interface{})
	gid := uint64(asF64(payload["GroupID"]))
	if gid == 0 {
		t.Fatal("未取到 groupID")
	}

	// 错误密码 -> 拒绝
	bob.send(GROUP_JOIN_REQUEST, []interface{}{float64(gid), "wrong"})
	if m := bob.wait(t, GROUP_OP_RESP); asBool(m.Message[0]) || asStr(m.Message[1]) != "入群密码错误" {
		t.Fatalf("错误密码应被拒: %v", m.Message)
	}

	// 正确密码 -> 直接入群（无需审批）
	bob.send(GROUP_JOIN_REQUEST, []interface{}{float64(gid), "secret"})
	rp := bob.wait(t, GROUP_JOIN_RESULT_PUSH)
	if !asBool(rp.Message[2]) || len(rp.Message[3].([]interface{})) != 2 {
		t.Fatalf("凭密码应直接入群且花名册含2人: %v", rp.Message)
	}
	if mc := alice.wait(t, GROUP_MEMBER_CHANGE_PUSH); !asBool(mc.Message[1]) {
		t.Fatalf("群主应收到新成员加入推送: %v", mc.Message)
	}
	t.Log("入群密码流程通过")
}

// 群管理：授权/转移+退出/群主退出守卫
func TestIM_GroupManage(t *testing.T) {
	dir := t.TempDir()
	uuidFilePath = filepath.Join(dir, "uuid.json")
	resetServerState()
	if err := StoreInit(filepath.Join(dir, "convnet_gm.db")); err != nil {
		t.Fatalf("StoreInit: %v", err)
	}
	defer store.db.Close()

	alice := newFakeClient()
	bob := newFakeClient()
	alice.registerAccount(t, "alice", "pw", "Alice", "") // userID 1
	bob.registerAccount(t, "bob", "pw", "Bob", "")       // userID 2

	g, _ := store.CreateGroup(1, "gm", "")
	store.AddGroupMember(g.GroupID, 2) // bob 入群
	gid := g.GroupID

	// 群主有其他成员时直接退出 -> 拒绝
	alice.send(GROUP_LEAVE, []interface{}{float64(gid)})
	if m := alice.wait(t, GROUP_OP_RESP); asBool(m.Message[0]) {
		t.Fatalf("群主有成员时退出应被拒: %v", m.Message)
	}

	// 授予 bob 管理员
	alice.send(GROUP_MANAGE, []interface{}{float64(gid), "grant", float64(2)})
	if m := alice.wait(t, GROUP_OP_RESP); !asBool(m.Message[0]) {
		t.Fatalf("授予管理员应成功: %v", m.Message)
	}
	if g2, _ := store.GetGroup(gid); len(g2.Admins) != 1 || g2.Admins[0] != 2 {
		t.Fatalf("bob 应为管理员: %v", g2.Admins)
	}

	// 转移群主给 bob 并退出
	alice.send(GROUP_MANAGE, []interface{}{float64(gid), "handover", float64(2)})
	if m := alice.wait(t, GROUP_OP_RESP); !asBool(m.Message[0]) {
		t.Fatalf("转移并退出应成功: %v", m.Message)
	}
	g3, _ := store.GetGroup(gid)
	if g3.OwnerID != 2 {
		t.Fatalf("群主应转为 bob(2)，实际 %d", g3.OwnerID)
	}
	for _, mm := range g3.Members {
		if mm == 1 {
			t.Fatal("原群主应已退出")
		}
	}
	t.Log("群管理流程通过")
}

// registerLogin 账号登录，成功返回 PublicID。
func (fc *fakeClient) registerLogin(t *testing.T, account, password, mac string) string {
	t.Helper()
	fc.send(ACCOUNT_LOGIN, []interface{}{account, password, mac})
	resp := fc.wait(t, WS_REGISTE_RESP)
	return resp.Message[0].(string)
}
