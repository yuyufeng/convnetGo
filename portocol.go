package main

import "net"

const (
	ALL_DATA = iota //0       //发送数据
	WS_REGISTE
	WS_REGISTE_RESP
	WS_REGISTE_FAIL
	C_GETWS_SERVER_INFO
	C_GETWS_SERVER_INFO_RESP
	C_CONNTOWS_SERVER
	C_CONNTOWS_PEERCALL
	C_CONNTOWS_PEERCALL_RESP
	C_CONNTOWS_PEERCALL_RESP_NOTONLINE
	C_CONNTOWS_PEERCALL_FIN
	C_CONNTOWS_PEERDISCONNECT

	// ---- IM 层协议（QQ 风格）：好友 ----
	FRIEND_SEARCH      //12 [keyword] -> FRIEND_SEARCH_RESP
	FRIEND_SEARCH_RESP //13 [[{UserID,PublicID,Nick,Online}...]]
	FRIEND_REQUEST     //14 [toUserID, greeting]
	FRIEND_REQUEST_PUSH//15 推送给对方 [fromUserID, fromPublicID, fromNick, greeting]
	FRIEND_ACCEPT      //16 [fromUserID, accept(bool)]
	FRIEND_ACCEPT_PUSH //17 回推双方 [userID, publicID, nick, cvnIP, mac, accepted(bool)]
	FRIEND_LIST        //18 []
	FRIEND_LIST_RESP   //19 [[{UserID,PublicID,Nick,Online,CvnIP,Mac}...]]
	FRIEND_REMOVE      //20 [peerUserID]
	FRIEND_REMOVE_PUSH //21 推送给对方 [fromUserID]

	// ---- IM 层协议：用户组 ----
	GROUP_CREATE            //22 [groupName] -> GROUP_OP_RESP（服务端强制 <=5）
	GROUP_SEARCH            //23 [keyword]
	GROUP_SEARCH_RESP       //24 [[{GroupID,Name,OwnerID,MemberCount}...]]
	GROUP_JOIN_REQUEST      //25 [groupID]
	GROUP_JOIN_PUSH         //26 推送群主 [groupID, applicantUserID, applicantNick]
	GROUP_JOIN_APPROVE      //27 [groupID, applicantUserID, approve(bool)]（强制 <=10）
	GROUP_JOIN_RESULT_PUSH  //28 推送申请人 [groupID, groupName, approved(bool), [成员花名册]]
	GROUP_LEAVE             //29 [groupID]
	GROUP_LIST              //30 []
	GROUP_LIST_RESP         //31 [[{GroupID,Name,OwnerID,[成员...] }...]]
	GROUP_MEMBER_CHANGE_PUSH//32 推送组员 [groupID, joined(bool), {UserID,PublicID,Nick,CvnIP,Mac}]
	GROUP_OP_RESP           //33 通用应答 [ok(bool), message, payload]

	// ---- IM 层协议：在线状态 ----
	PRESENCE_NOTIFY    //34 推送 [userID, online(bool)]
	PRESENCE_SUBSCRIBE //35 [ [userID...] ] 预留

	// ---- IM 层协议：聊天（服务器存储转发，非网络帧）----
	CHAT_SEND         //36 [scope("u"|"g"), targetID, msgID, contentType, richText]
	CHAT_DELIVER      //37 推送 [scope, fromUserID, fromNick, groupID, msgID, contentType, richText, ts]
	CHAT_ACK          //38 [msgID]
	CHAT_HISTORY_REQ  //39 [scope, targetID, beforeTs, limit]
	CHAT_HISTORY_RESP //40 [[消息...]]

	// ---- 网络层：黑白名单（随身份同步）----
	ACL_SET      //41 [mode("black"|"white"), [black...], [white...]]
	ACL_GET      //42 []
	ACL_GET_RESP //43 [mode, [black...], [white...]]

	// ---- 网络层：P2P 失败回退，经信令服务器盲转发 ----
	RELAY_DATA     //44 [targetPublicID, base64(AES帧)]
	RELAY_DATA_FWD //45 推送 [fromPublicID, base64(AES帧)]
	RELAY_OPEN     //46 [targetPublicID]
	RELAY_CLOSE    //47 [targetPublicID]

	// 账号密码认证（替代 UUID 隐式身份）
	ACCOUNT_REGISTER //48 [account, password, nick, mac] -> WS_REGISTE_RESP[PublicID,nick] | WS_REGISTE_FAIL[reason]
	ACCOUNT_LOGIN    //49 [account, password, mac]        -> 同上

	// 群管理（群主/管理员）：[groupID, action, targetUserID]
	// action: kick/grant/revoke/transfer/handover/disband
	GROUP_MANAGE //50

	UNKNKOWN //51
)

const (
	CLIENTMODE = iota
	MAINCLIENT
	CLIENT
	SERVER
)

func sendToConn(conn net.Conn, msgtype int, message []interface{}) {
	var clientMessage clientMessage
	clientMessage.CMDType = msgtype
	clientMessage.Message = message
	clientMessage.Version = "1.0"
	jsonStr := ToJson(clientMessage) + "\r\n"
	//封包，发送两位大端序的长度
	len := (int32)(len(jsonStr))

	send := append(IntToBytes(len), []byte(jsonStr)...)
	if conn != nil {
		_, err := conn.Write(send)
		if err != nil {
			conn.Close()
		}
	}
}
