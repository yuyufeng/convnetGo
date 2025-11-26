package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"sort"
	"strconv"
	"strings"

	"github.com/labstack/gommon/log"
	"github.com/pion/webrtc/v4"
)

func ToJson(v interface{}) string {
	data, err := json.Marshal(v)
	if err != nil {
		panic(err)
	}
	if string(data) != "null" {
		return string(data)
	} else {
		return "{}"
	}
}

func allowConnect(w http.ResponseWriter, r *http.Request) {
	//获取参数PublicID
	client.AutoConnectPassword = r.URL.Query().Get("autoConnectPassword")
	w.Write([]byte(`{"status": "success", "message": "操作完成"}`))
}
func clientDisconnect(w http.ResponseWriter, r *http.Request) {
	client.logout()
	client.RetryConnect = false
	w.Write([]byte(`{"status": "success", "message": "操作完成"}`))
}

func clientConnectToServer(w http.ResponseWriter, r *http.Request) {
	client.logout()
	client.RetryConnect = true
	ConnectServer(client.Server, client.ServerPort)
	w.Write([]byte(`{"status": "success", "message": "操作完成"}`))
}

func removePublicId(w http.ResponseWriter, r *http.Request) {
	publicID := r.URL.Query().Get("PublicID")
	if publicID != "" {
		DelUserFromUserList(publicID)
		AppendLineToFile("publicid.txt", publicID)
		fmt.Fprintf(w, "OK")
	} else {
		fmt.Fprintf(w, "ERROR")
	}
}
func connectToPublicID(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	//获取参数PublicID
	publicID := r.URL.Query().Get("PublicID")
	passWord := r.URL.Query().Get("PassWord")
	log.Info("正在连接到PublicID:", publicID)
	upperpbid := strings.ToUpper(publicID)
	if !strings.HasPrefix(upperpbid, strings.ToUpper("CVNID://"+client.Server+":"+client.ServerPort)) {
		w.Write([]byte(`{"status": "error", "message": "此CVNID和本服务器信息不匹配，不予连接"}`))
		log.Error(publicID, "此CVNID和本服务器信息不匹配，不予连接")
	}

	publicID = strings.Split(publicID, "@")[1]
	//连接到publicID

	if publicID == "" {
		log.Info("请输入PublicID")
		w.Write([]byte(`{"status": "error", "message": "请输入PublicID"}`))
		return
	}

	if publicID == client.PublicID {
		log.Info("不能连接到自己")
		w.Write([]byte(`{"status": "error", "message": "不能连接到自己"}`))
		return
	}

	var user *User
	user = GetUserByPublicID(publicID)
	if user == nil {
		user = new(User)
		user.PublicID = publicID
		user.AccessPass = passWord
		// clientinfo := formatjsontoMainClientInfo(clientMessage)
	}
	user.IsOnline = true //默认在线
	user.AccessPass = passWord
	//如果文件中不存在这个行，则追加保存publicid到autoConnectPeer.txt
	//if !IsPublicIDInAutoConnectPeer(r.URL.Query().Get("PublicID")) {
	DeleteLineFromFile("autoConnectPeer.txt", publicID)
	AppendLineToFile("autoConnectPeer.txt", r.URL.Query().Get("PublicID")+"@"+passWord)
	//} else {
	//	log.Info("此PublicID，已存在于autoConnectPeer.txt")
	//}
	//TODO
	RenewUserList(user)

	if user.Pc != nil {
		log.Info(user.Pc.ConnectionState()) //获取当前状态
		if user.Pc.ConnectionState() == webrtc.PeerConnectionStateConnecting ||
			user.Pc.ConnectionState() == webrtc.PeerConnectionStateNew { //如果当前已经在连接，则断一下，等对方连接
			//尝试方案1的时候中断连接，开始尝试方案2
			//peerConnectionUpdate(user, "-1")
			user.Pc.Close()
			user.Pc = nil

			user.AllowConnect = true
			sendToConn(client.g_conn, C_CONNTOWS_PEERCALL, []interface{}{user.PublicID, "", "-1", user.AccessPass})
			log.Info("C_CONNTOWS_PEERCALL, -1")
			return
		}
	}

	go peerConnectionUpdate(user, "0") //更新SDP
	//user.IamCaller = true

	w.Write([]byte(ToJson(client)))
}

func updateUserInfo(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	// 解析请求体中的 JSON 数据
	var updateData map[string]interface{}
	if err := json.NewDecoder(r.Body).Decode(&updateData); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	// 更新 client 对象
	if clientMode, ok := updateData["ClientMode"].(float64); ok {
		client.ClientMode = int(clientMode)
	}
	if clientID, ok := updateData["ClientID"].(string); ok {
		client.ClientID = clientID
	}
	if server, ok := updateData["Server"].(string); ok {
		client.Server = server
	}
	if uuid, ok := updateData["UUID"].(string); ok {
		client.UUID = uuid
	}
	if serverPort, ok := updateData["ServerPort"].(string); ok {
		client.ServerPort = serverPort
	}
	if serverTurnPort, ok := updateData["ServerTurnPort"].(string); ok {
		client.ServerTurnPort = serverTurnPort
	}

	if publicID, ok := updateData["PublicID"].(string); ok {
		client.PublicID = publicID
	}
	if mac, ok := updateData["Mac"].(string); ok {
		client.Mac = mac
	}

	//将client信息保存到文件

	if err := saveClientConfig("convnet.json"); err != nil {
		fmt.Println("Error saving client configuration:", err)
	}

	//client重新连接并登记服务
	// client.g_conn.Close()
	// time.Sleep(time.Second * 2)
	// ConnectServer(client.Server, client.ServerPort)

	// 返回成功响应
	w.WriteHeader(http.StatusOK)
	w.Write([]byte(`{"status": "success"}`))
}

func getClientInfo(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.Write([]byte(ToJson(client)))
}
// 发送聊天消息
func sendChatMessageAPI(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	if r.Method != "POST" {
		w.WriteHeader(http.StatusMethodNotAllowed)
		w.Write([]byte(`{"status": "error", "message": "Method not allowed"}`))
		return
	}

	var reqBody struct {
		To      string `json:"to"`      // 接收者 PublicID，空表示广播
		Content string `json:"content"` // 消息内容
	}

	if err := json.NewDecoder(r.Body).Decode(&reqBody); err != nil {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`{"status": "error", "message": "Invalid request body"}`))
		return
	}

	if reqBody.Content == "" {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`{"status": "error", "message": "Content is required"}`))
		return
	}

	msg, err := SendChatMessage(reqBody.To, reqBody.Content)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(`{"status": "error", "message": "` + err.Error() + `"}`))
		return
	}

	w.Write([]byte(ToJson(map[string]interface{}{
		"status":  "success",
		"message": msg,
	})))
}

// 获取聊天历史
func getChatHistoryAPI(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	publicID := r.URL.Query().Get("publicId")
	limitStr := r.URL.Query().Get("limit")
	limit := 100
	if limitStr != "" {
		if l, err := strconv.Atoi(limitStr); err == nil {
			limit = l
		}
	}

	history := GetChatHistory(publicID, limit)
	w.Write([]byte(ToJson(map[string]interface{}{
		"status":   "success",
		"messages": history,
	})))
}

// 清空聊天历史
func clearChatHistoryAPI(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	ClearChatHistory()
	w.Write([]byte(`{"status": "success", "message": "Chat history cleared"}`))
}

func getUserList(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json") // 设置响应头
	//w.Write([]byte("ToJson(allUserlist)"))
	userlist := make(map[string]*User)

	//返回user基本信息，和在线状态

	connUserMap.Range(func(key, value interface{}) bool {
		user := value.(*User) // 假设 value 是 *User 类型
		//fmt.Print("needsent", packet)
		userlist[user.PublicID] = user
		user.IsConnected = user.Dc != nil && user.Dc.ReadyState() == webrtc.DataChannelStateOpen
		//服务器是否中继
		user.IsRelay = false
		//获取当前的selectedPair的连接信息
		if user.Pc != nil && user.Pc.ICEConnectionState() == webrtc.ICEConnectionStateConnected {
			selectedPair, err := user.Pc.SCTP().Transport().ICETransport().GetSelectedCandidatePair()
			if err != nil {
				log.Error("获取所选候选对失败:", err)
			}

			if selectedPair != nil {
				// fmt.Printf("Selected Candidate Pair:\nLocal: %s\nRemote: %s\n",
				// 	selectedPair.Local.String(),
				// 	selectedPair.Remote.String())
				if selectedPair.Local.Typ == webrtc.ICECandidateTypeRelay {
					user.IsRelay = true
				}
			}
		} else {
			return true
		}

		user.IceState = user.Pc.ICEConnectionState().String()
		//log.Info("当前 ICEConnectionState:", user.Pc.ICEConnectionState())
		return true
	})

	//将userlist转换为json,按publicid最后:的数字排序后输出例子：d356970b5c067233ce922ebdb00b99c6:11

	allUserlist := make([]interface{}, 0)
	for _, user := range userlist {
		allUserlist = append(allUserlist, user)
	}

	// 自定义排序：提取 PublicID 最后一个 : 后的数字进行排序
	sort.Slice(allUserlist, func(i, j int) bool {
		// 提取 ID 的数字部分

		idI := allUserlist[i].(*User).PublicID
		idJ := allUserlist[j].(*User).PublicID

		// 分割字符串，取最后部分作为数字
		partsI := strings.Split(idI, ":")
		partsJ := strings.Split(idJ, ":")

		suffixI := "0"
		suffixJ := "0"

		if len(partsI) > 0 {
			suffixI = partsI[len(partsI)-1]
		}
		if len(partsJ) > 0 {
			suffixJ = partsJ[len(partsJ)-1]
		}

		// 转换为整数比较
		numI, _ := strconv.Atoi(suffixI)
		numJ, _ := strconv.Atoi(suffixJ)

		return numI < numJ
	})

	w.Write([]byte(ToJson(allUserlist)))
}
