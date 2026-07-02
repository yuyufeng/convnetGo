package main

// im_types.go —— IM/社交层与网络层共享的数据结构（服务端持久化 + 线协议 payload）

// UserRecord 一个已注册用户的静态身份信息（持久化）。
type UserRecord struct {
	UserID   uint64 `json:"UserID"`
	PublicID string `json:"PublicID"` // md5(account):userID
	Account  string `json:"Account"`  // 唯一登录账号
	PassHash string `json:"PassHash"` // bcrypt 密码哈希
	Nick     string `json:"Nick"`
	Mac      string `json:"Mac"`   // 最近一次登记的 MAC
	CvnIP    string `json:"CvnIP"` // 由 userID 推导，缓存

	RelayBlocked   bool `json:"RelayBlocked"`   // true=禁止该用户使用服务器中继转发
	RelayLimitKBps int  `json:"RelayLimitKBps"` // 中继限速，KB/s，0=不限（回退全局限速）
}

// GroupRecord 一个用户组（持久化）。Members 含群主自身。
type GroupRecord struct {
	GroupID      uint64   `json:"GroupID"`
	Name         string   `json:"Name"`
	OwnerID      uint64   `json:"OwnerID"`
	Members      []uint64 `json:"Members"`
	Admins       []uint64 `json:"Admins"`       // 管理员（群主之外的协管，可踢普通成员）
	CreatedTs    int64    `json:"CreatedTs"`
	JoinPassword string   `json:"JoinPassword"` // bcrypt 哈希；非空=凭密码免审批加入，空=需群主审批
}

// FriendRequest 待处理的好友申请（离线保留，登录后下发）。
type FriendRequest struct {
	FromUserID uint64 `json:"FromUserID"`
	FromNick   string `json:"FromNick"`
	Greeting   string `json:"Greeting"`
	Ts         int64  `json:"Ts"`
}

// GroupJoinRequest 待处理的入群申请（离线保留，群主登录后下发）。
type GroupJoinRequest struct {
	GroupID       uint64 `json:"GroupID"`
	ApplicantID   uint64 `json:"ApplicantID"`
	ApplicantNick string `json:"ApplicantNick"`
	Ts            int64  `json:"Ts"`
}

// OfflineMsg 离线聊天消息（登录后下发，收到 CHAT_ACK 出队）。
type OfflineMsg struct {
	Scope       string `json:"Scope"` // "u" 单聊 | "g" 群聊
	FromUserID  uint64 `json:"FromUserID"`
	FromNick    string `json:"FromNick"`
	GroupID     uint64 `json:"GroupID"` // 群聊时有效
	MsgID       string `json:"MsgID"`
	ContentType string `json:"ContentType"` // "rich" | "text"
	RichText    string `json:"RichText"`
	Ts          int64  `json:"Ts"`
}

// AclRecord 网络层黑白名单（随身份同步）。
type AclRecord struct {
	Mode  string   `json:"Mode"` // "black"（默认，黑名单外全放行）| "white"（白名单内才放行）
	Black []uint64 `json:"Black"`
	White []uint64 `json:"White"`
}
