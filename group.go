package main

import (
	"fmt"
	"net"

	"github.com/labstack/gommon/log"
	"github.com/pion/webrtc/v4"
)

type noCopy struct{}

// Lock is a no-op used by -copylocks checker from `go vet`.
func (*noCopy) Lock()   {}
func (*noCopy) Unlock() {}

type User struct {
	IsConnected  bool //是否在线
	IsRelay      bool //是否中转
	IceState     string
	PublicID     string
	AccessPass   string //对方的访问密码
	UserNickName string
	AllowConnect bool     //标志不要验证密码，允许连接，主动发起请求的时候要标识信任，否则过不了本地认证
	MacAddress   string   //MAC地址
	CommAddress  net.Addr //实际通讯端口
	Con_send     int64
	Con_recv     int64
	Needpass     bool
	Con_conType  int //1 udp 2 tcptrans
	CvnIP        string
	IsOnline     bool
	//IamCaller      bool
	//Calltimes      int

	Pc  *webrtc.PeerConnection
	Dc  *webrtc.DataChannel
	SDP string //链接信息
}

func RenewUserList(user *User) {
	log.Debug("INITUSER", user)
	connAddrMap.Store(user.MacAddress, user) //
	connUserMap.Store(user.PublicID, user)
	//setupArpinfo(user.MacAddress, user.CvnIP)
}

func DelUserFromUserList(PublicID string) {
	log.Info("<<<<移除，DelUserFromUserList", PublicID)
	connUserMap.Delete(PublicID)
	DeleteLineFromFile("autoConnectPeer.txt", PublicID)
}

// 🖥️ A端 (Caller)                                    🖥️ B端 (Callee)
// ──────────────────────────────────────────────────────────────────────
// [1] CreateOffer()
//
//	↓
//
// [2] SetLocalDescription(offer)
//
//	↓
//
// [3] 发送 offer 给对方 ➔➔➔➔➔➔➔➔➔➔➔
//
//												 [4] SetRemoteDescription(offer)
//	                                             [5] CreateAnswer()
//	                                                  ↓
//	                                             [6] SetLocalDescription(answer)
//	                                                  ↓
//	                                             [7] 发送 answer 回去 ➔➔➔➔➔➔➔
//
// [8] SetRemoteDescription(answer)
// ──────────────────────────────────────────────────────────────────────
//
//	       ↓                                         ↓
//	(交换ICE Candidate)
//	       ↓                                         ↓
//	(连接成功 / PeerConnectionStateConnected)
//
// 重新創建PeerConnection

func reInitPc(user *User) bool {
	log.Info("初始化PeerConnection")
	if user.Pc != nil { //重新创建
		//user.Pc.Close()
		user.Pc = nil
		user.Dc = nil
	}
	// 1. 新建  PeerConnection
	Pc, err := webrtc.NewPeerConnection(webrtc.Configuration{
		ICEServers: []webrtc.ICEServer{
			{
				URLs: []string{
					"turn:" + client.Server + ":" + client.ServerTurnPort + "?transport=tcp",
					"turn:" + client.Server + ":" + client.ServerTurnPort + "?transport=udp",
				},
				Username:       client.ServerTurnUser,
				Credential:     client.ServerTurnPass,
				CredentialType: webrtc.ICECredentialTypePassword,
			},
		},
		ICETransportPolicy: webrtc.ICETransportPolicyAll,
	})

	if err != nil {
		log.Error(err)
		return false
	}

	Pc.OnICECandidate(func(c *webrtc.ICECandidate) {
		// if c == nil {
		// 	log.Info("ICE Candidate gathering complete")
		// 	return
		// }
		//log.Infof("New ICE Candidate: %s", c.ToJSON().Candidate)
	})

	Pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		log.Info("ICE Connection State has changed: ", state.String())
		// 获取当前的连接统计信息
		// stats := Pc.GetStats()
		// // 遍历所有ICE传输统计信息
		// for _, stat := range stats {
		// 	// 检查ICE候选者对信息
		// 	if candidatePair, ok := stat.(webrtc.ICECandidatePairStats); ok && candidatePair.State == "succeeded" {
		// 		log.Info("当前活跃的ICE连接信息:")
		// 		log.Info("  - 本地候选者ID:", candidatePair.LocalCandidateID)
		// 		log.Info("  - 远程候选者ID:", candidatePair.RemoteCandidateID)
		// 		log.Info("  - 字节发送:", candidatePair.BytesSent)
		// 		log.Info("  - 字节接收:", candidatePair.BytesReceived)
		// 	}

		// 	// 检查本地候选者信息
		// 	if localCandidate, ok := stat.(webrtc.ICECandidateStats); ok && localCandidate.Type == "local" {
		// 		log.Info("本地端点信息:")
		// 		log.Info("  - IP:", localCandidate.IP)
		// 		log.Info("  - 端口:", localCandidate.Port)
		// 		log.Info("  - 协议:", localCandidate.Protocol)
		// 		log.Info("  - 候选者类型:", localCandidate.CandidateType)
		// 	}

		// 	// 检查远程候选者信息
		// 	if remoteCandidate, ok := stat.(webrtc.ICECandidateStats); ok && remoteCandidate.Type == "remote" {
		// 		log.Info("远程端点信息:")
		// 		log.Info("  - IP:", remoteCandidate.IP)
		// 		log.Info("  - 端口:", remoteCandidate.Port)
		// 		log.Info("  - 协议:", remoteCandidate.Protocol)
		// 		log.Info("  - 候选者类型:", remoteCandidate.CandidateType)
		// 	}
		// }
	})
	// pc2 监听 DataChannel
	Pc.OnDataChannel(func(dc *webrtc.DataChannel) {
		log.Info("DataChannel created:", dc.Label())

		// 监听 DataChannel 状态变化
		dc.OnOpen(func() {
			log.Info("✅ DataChannel 已打开:")
			log.Info("  - Label:", dc.Label())
			log.Info("  - ID:", dc.ID())
			log.Info("  - Ordered:", dc.Ordered())
			log.Info("  - BufferedAmount:", dc.BufferedAmount())
			log.Info("  - ReadyState:", dc.ReadyState().String())
			log.Info("  - User Info: CvnIP=", user.CvnIP, "Mac=", user.MacAddress, "PublicID=", user.PublicID)
			user.Dc = dc
		})

		dc.OnClose(func() {
			log.Info("DataChannel已关闭:", dc.Label())
		})

		dc.OnMessage(func(msg webrtc.DataChannelMessage) {
			user.Con_recv = user.Con_recv + int64(len(msg.Data))
			// 检查是否是聊天消息
			if IsChatMessage(msg.Data) {
				HandleIncomingChatMessage(msg.Data, user)
				return
			}
			fmt.Println("dc <<<:", len(msg.Data))
			writePacket(msg.Data)
		})
	})

	//4. 然后就可以等待连接状态变化
	Pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		fmt.Println("PeerConnection state:", state)

		if state == webrtc.PeerConnectionStateClosed {
			// if !user.IamCaller {
			// 	//如果不是由我发起的，则进行一次打洞发起
			// 	//Pc.Close()
			// 	//user.Pc = nil

			// 	if user.Calltimes < 3 {
			// 		log.Info(user.PublicID, "retry", user.Calltimes, "次重试")
			// 		peerConnectionUpdate(user, "0")
			// 	} else {
			// 		log.Info("尝试失败,退出重试")
			// 		peerConnectionUpdate(user, "4") //通知对方结束尝试
			// 		user.Calltimes = 0
			// 		return
			// 	}
			// 	user.IamCaller = true
			// }
		}

		//if state == webrtc.PeerConnectionStateFailed {
		// if err := Pc.Close(); err != nil {
		// 	log.Printf("Failed to close peer connection: %v", err)
		// } else {
		// 	fmt.Println("PeerConnection closed successfully")
		// }
		// user.Pc = nil
		// user.Dc = nil
		// Pc = nil
		//}
	})

	// 设置媒体轨道回调
	Pc.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
		fmt.Printf("Received track: %s\n", track.ID())
	})

	if err != nil {
		log.Error(err)
	}

	user.Pc = Pc
	return true
}
func peerConnectionUpdate(user *User, step string) {

	if user.Pc != nil && (step == "0" || step == "-1") {
		// if user.Pc.ConnectionState() == webrtc.PeerConnectionStateConnecting {
		// 	//当前如果正在连接，则先不做其他操作
		// 	log.Info("当前正在连接", user.Pc.ConnectionState(), "，请稍等...")
		// 	return
		// }
	}
	if step == "0" { //客户端发起

		log.Info("step0")

		if !reInitPc(user) {
			return
		}

		Pc := user.Pc

		log.Info("当前 SignalingState:", Pc.SignalingState())
		log.Debug("当前 ICEConnectionState:", Pc.ICEConnectionState())
		log.Debug("当前 ConnectionState:", Pc.ConnectionState())

		// 2. 建立 DataChannel
		dc, err := Pc.CreateDataChannel("data", nil)
		if err != nil {
			panic(err)
		}

		dc.OnOpen(func() {
			//user.Calltimes = 0
			log.Info("✅", "public:", user.PublicID, "CVNIP:", user.CvnIP, ">>>P2P通讯channel已经打开")

			log.Info("✅ DataChannel 已打开:")
			log.Info("  - Label:", dc.Label())
			log.Info("  - ID:", dc.ID())
			log.Info("  - Ordered:", dc.Ordered())
			log.Info("  - BufferedAmount:", dc.BufferedAmount())
			log.Info("  - ReadyState:", dc.ReadyState().String())
			log.Info("  - User Info: CvnIP=", user.CvnIP, "Mac=", user.MacAddress, "PublicID=", user.PublicID)
			user.Dc = dc
		})

		// 3. 设置 DataChannel回调（接收数据）
		dc.OnMessage(func(msg webrtc.DataChannelMessage) {
			user.Con_recv = user.Con_recv + int64(len(msg.Data))
			// 检查是否是聊天消息
			if IsChatMessage(msg.Data) {
				HandleIncomingChatMessage(msg.Data, user)
				return
			}
			log.Debug("dcc <<<", len(msg.Data))
			writePacket(msg.Data)
		})

		offer, err := Pc.CreateOffer(nil)
		if err != nil {
			log.Error(err)
		}
		err = Pc.SetLocalDescription(offer)
		if err != nil {
			log.Error(err)
		}

		gatherComplete := webrtc.GatheringCompletePromise(Pc)
		<-gatherComplete

		localDesc := Pc.LocalDescription()

		log.Debug("offersdp", offer.SDP)
		sendToConn(client.g_conn, C_CONNTOWS_PEERCALL, []interface{}{user.PublicID, localDesc.SDP, "1", user.AccessPass})

	}

	if step == "1" {
		log.Info("step1")
		//user.Calltimes = user.Calltimes + 1
		reInitPc(user)
		//user.IamCaller = false //接受打洞
		Pc := user.Pc
		if Pc.SignalingState() != webrtc.SignalingStateStable {
			log.Info("当前状态无法进行PeerConnection step1")
			return
		}

		log.Info("当前 SignalingState:", Pc.SignalingState())
		log.Debug("当前 ICEConnectionState:", Pc.ICEConnectionState())
		log.Debug("当前 ConnectionState:", Pc.ConnectionState())

		if Pc != nil {
			remoteOffer := webrtc.SessionDescription{
				Type: webrtc.SDPTypeOffer,
				SDP:  user.SDP,
			}

			// 3. 把远端 Offer 设置进去
			err := Pc.SetRemoteDescription(remoteOffer)
			if err != nil {
				log.Error(err)
				return
			}

			// 4. 创建 Answer
			answer, err := Pc.CreateAnswer(nil)
			if err != nil {
				log.Error(err)
				return
			}

			// 5. 设置本地描述（SetLocalDescription）
			err = Pc.SetLocalDescription(answer)
			if err != nil {
				log.Error(err)
			}

			fmt.Print("setRemoteOffer")
			// 6. 打印生成的 Answer SDP（这个要发送回去给对方）
			gatherComplete := webrtc.GatheringCompletePromise(Pc)
			<-gatherComplete
			// 6. 拿最终带candidate的完整Answer SDP
			localDesc := Pc.LocalDescription()

			//log.Debug(localDesc.SDP)
			sendToConn(client.g_conn, C_CONNTOWS_PEERCALL, []interface{}{user.PublicID, localDesc.SDP, "2", user.AccessPass})

		}
	}
	if step == "2" {
		Pc := user.Pc
		if Pc == nil {
			return
		}

		if Pc.SignalingState() != webrtc.SignalingStateHaveLocalOffer {
			log.Error("状态不是have-Local-offer，无法进行PeerConnection step2,完全释放当前的Peerconnection")
			user.Pc = nil
			user.Dc = nil //出现状态错误，完全释放当前状态
			return
		}

		log.Debug("当前 SignalingState:", Pc.SignalingState())
		log.Debug("当前 ICEConnectionState:", Pc.ICEConnectionState())
		log.Debug("当前 ConnectionState:", Pc.ConnectionState())

		log.Debug(step, user.SDP)
		answer := webrtc.SessionDescription{
			Type: webrtc.SDPTypeAnswer,
			SDP:  user.SDP,
		}
		log.Info("have-remote-offer")

		err := Pc.SetRemoteDescription(answer)
		if err != nil {
			log.Error(err)
		}
		log.Debug("step2:", answer.SDP)

	}
	if step == "-1" {
		//清除链接，准备主动连接对方
		//user.Pc.Close()
		user.Pc = nil
		user.Dc = nil
		go peerConnectionUpdate(user, "0")

		log.Info("对方请求由对方发起请求，释放当前链接,开始peerConnectionUpdate 0")

	}
	if step == "E" {
		user.Pc = nil
		user.Dc = nil
		user.AccessPass = "ERROR"
		log.Info("访问密码错误，对方拒绝连接")
		return
	}

}

func AddAllowList(user *User) {
	connAddrMap.Store(user.CommAddress, user)
}

func GetUserByMac(mac string) (user *User) {
	// 使用 Load 方法查找键
	//log.Info(mac.String())

	value, ok := connAddrMap.Load(mac)
	if ok {
		return value.(*User)
	}
	//log.Error("user not found", mac)

	return nil
}

func GetUserByPublicID(PublicID string) *User {
	//遍歷connMap
	var foundUser *User

	connUserMap.Range(func(key, value interface{}) bool {
		//log.Info(key, ":", value)
		user := value.(*User)
		if user.PublicID == PublicID {
			foundUser = user
			return false
		}
		return true
	})
	return foundUser
}
