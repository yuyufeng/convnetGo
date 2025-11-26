package main

import (
	"encoding/json"
	"sync"
	"time"

	"github.com/labstack/gommon/log"
	"github.com/pion/webrtc/v4"
)

// 聊天消息前缀，用于区分网络数据包和聊天消息
const CHAT_MSG_PREFIX = "CVNCHAT:"

// ChatMessage 聊天消息结构
type ChatMessage struct {
	ID        string `json:"id"`
	From      string `json:"from"`      // 发送者 PublicID
	FromName  string `json:"fromName"`  // 发送者昵称
	To        string `json:"to"`        // 接收者 PublicID，空表示广播
	ToName    string `json:"toName"`    // 接收者昵称
	Content   string `json:"content"`   // 消息内容
	Timestamp int64  `json:"timestamp"` // 时间戳
	IsMe      bool   `json:"isMe"`      // 是否是自己发送的
}

// 聊天消息历史存储
var chatHistory = struct {
	sync.RWMutex
	messages []ChatMessage
}{
	messages: make([]ChatMessage, 0),
}

// 最大消息历史数量
const MAX_CHAT_HISTORY = 500

// generateMessageID 生成消息ID
func generateMessageID() string {
	return time.Now().Format("20060102150405") + "_" + generateUUID()[:8]
}

// generateUUID 生成简单UUID
func generateUUID() string {
	b := make([]byte, 16)
	for i := range b {
		b[i] = byte(time.Now().UnixNano() % 256)
	}
	return string(b)
}

// SendChatMessage 发送聊天消息
func SendChatMessage(toPublicID string, content string) (*ChatMessage, error) {
	msg := &ChatMessage{
		ID:        generateMessageID(),
		From:      client.PublicID,
		FromName:  client.ClientID,
		To:        toPublicID,
		Content:   content,
		Timestamp: time.Now().UnixMilli(),
		IsMe:      true,
	}

	// 获取接收者昵称
	if toPublicID != "" {
		user := GetUserByPublicID(toPublicID)
		if user != nil {
			msg.ToName = user.UserNickName
		}
	}

	// 序列化消息
	msgBytes, err := json.Marshal(msg)
	if err != nil {
		return nil, err
	}

	// 添加前缀
	chatData := []byte(CHAT_MSG_PREFIX + string(msgBytes))

	// 发送消息
	if toPublicID == "" {
		// 广播消息
		sendChatToAll(chatData)
	} else {
		// 私聊消息
		sendChatToUser(toPublicID, chatData)
	}

	// 保存到历史记录
	saveChatMessage(*msg)

	return msg, nil
}

// sendChatToUser 发送消息给指定用户
func sendChatToUser(publicID string, data []byte) bool {
	user := GetUserByPublicID(publicID)
	if user == nil {
		log.Error("用户不存在:", publicID)
		return false
	}

	if user.Dc == nil || user.Dc.ReadyState() != webrtc.DataChannelStateOpen {
		log.Error("用户未连接:", publicID)
		return false
	}

	err := user.Dc.Send(data)
	if err != nil {
		log.Error("发送聊天消息失败:", err)
		return false
	}

	log.Info("发送聊天消息到:", publicID)
	return true
}

// sendChatToAll 广播消息给所有已连接用户
func sendChatToAll(data []byte) {
	connUserMap.Range(func(key, value interface{}) bool {
		user := value.(*User)
		if user.Dc != nil && user.Dc.ReadyState() == webrtc.DataChannelStateOpen {
			user.Dc.Send(data)
		}
		return true
	})
}

// HandleIncomingChatMessage 处理收到的聊天消息
func HandleIncomingChatMessage(data []byte, fromUser *User) {
	// 移除前缀
	jsonData := data[len(CHAT_MSG_PREFIX):]

	var msg ChatMessage
	err := json.Unmarshal(jsonData, &msg)
	if err != nil {
		log.Error("解析聊天消息失败:", err)
		return
	}

	// 设置发送者信息
	msg.From = fromUser.PublicID
	msg.FromName = fromUser.UserNickName
	msg.IsMe = false

	log.Info("收到聊天消息:", msg.FromName, ":", msg.Content)

	// 保存到历史记录
	saveChatMessage(msg)
}

// saveChatMessage 保存消息到历史记录
func saveChatMessage(msg ChatMessage) {
	chatHistory.Lock()
	defer chatHistory.Unlock()

	chatHistory.messages = append(chatHistory.messages, msg)

	// 限制历史记录数量
	if len(chatHistory.messages) > MAX_CHAT_HISTORY {
		chatHistory.messages = chatHistory.messages[len(chatHistory.messages)-MAX_CHAT_HISTORY:]
	}
}

// GetChatHistory 获取聊天历史
func GetChatHistory(publicID string, limit int) []ChatMessage {
	chatHistory.RLock()
	defer chatHistory.RUnlock()

	result := make([]ChatMessage, 0)

	for _, msg := range chatHistory.messages {
		// 如果指定了 publicID，只返回与该用户相关的消息
		if publicID != "" {
			if msg.From != publicID && msg.To != publicID &&
				msg.From != client.PublicID && msg.To != client.PublicID {
				continue
			}
			// 只返回私聊消息
			if (msg.From == publicID && msg.To == client.PublicID) ||
				(msg.From == client.PublicID && msg.To == publicID) ||
				msg.To == "" { // 广播消息也显示
				result = append(result, msg)
			}
		} else {
			// 返回所有消息
			result = append(result, msg)
		}
	}

	// 限制返回数量
	if limit > 0 && len(result) > limit {
		result = result[len(result)-limit:]
	}

	return result
}

// ClearChatHistory 清空聊天历史
func ClearChatHistory() {
	chatHistory.Lock()
	defer chatHistory.Unlock()
	chatHistory.messages = make([]ChatMessage, 0)
}

// IsChatMessage 判断是否是聊天消息
func IsChatMessage(data []byte) bool {
	if len(data) < len(CHAT_MSG_PREFIX) {
		return false
	}
	return string(data[:len(CHAT_MSG_PREFIX)]) == CHAT_MSG_PREFIX
}
