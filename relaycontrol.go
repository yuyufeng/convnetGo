package main

// relaycontrol.go —— 服务器中继（RELAY_DATA）的准入与限速控制。
// 内存态：每用户策略缓存 + 令牌桶限速 + 已中继字节计数。
// 权限与限速由管理后台修改（同时写 store 持久化 + 更新此内存缓存）。

import (
	"sync"
	"time"
)

type relayPolicy struct {
	blocked  bool
	limitBps float64 // 每秒字节；0=不限（回退全局）
}

type tokenBucket struct {
	tokens float64
	lastNs int64
}

type RelayControl struct {
	mu        sync.Mutex
	globalBps float64
	policy    map[uint64]relayPolicy
	buckets   map[uint64]*tokenBucket
	bytes     map[uint64]uint64
}

var relayCtl *RelayControl

func NewRelayControl() *RelayControl {
	return &RelayControl{
		policy:  map[uint64]relayPolicy{},
		buckets: map[uint64]*tokenBucket{},
		bytes:   map[uint64]uint64{},
	}
}

// LoadFrom 启动时从 store 载入全局限速与各用户策略。
func (rc *RelayControl) LoadFrom(s *Store) {
	if s == nil {
		return
	}
	rc.mu.Lock()
	defer rc.mu.Unlock()
	rc.globalBps = float64(s.GlobalRelayLimit()) * 1024.0
	for _, u := range s.AllUsers() {
		rc.policy[u.UserID] = relayPolicy{
			blocked:  u.RelayBlocked,
			limitBps: float64(u.RelayLimitKBps) * 1024.0,
		}
	}
}

func (rc *RelayControl) SetUser(userID uint64, blocked bool, limitKBps int) {
	rc.mu.Lock()
	defer rc.mu.Unlock()
	rc.policy[userID] = relayPolicy{blocked: blocked, limitBps: float64(limitKBps) * 1024.0}
	delete(rc.buckets, userID) // 重置令牌桶
}

func (rc *RelayControl) SetGlobal(kbps int) {
	rc.mu.Lock()
	defer rc.mu.Unlock()
	rc.globalBps = float64(kbps) * 1024.0
}

func (rc *RelayControl) TotalBytes(userID uint64) uint64 {
	rc.mu.Lock()
	defer rc.mu.Unlock()
	return rc.bytes[userID]
}

// Allow 判断 userID 本次中继 n 字节是否放行（权限 + 令牌桶限速）。放行则计入字节。
func (rc *RelayControl) Allow(userID uint64, n int) bool {
	rc.mu.Lock()
	defer rc.mu.Unlock()

	p := rc.policy[userID]
	if p.blocked {
		return false
	}
	limit := p.limitBps
	if limit <= 0 {
		limit = rc.globalBps
	}
	if limit > 0 {
		now := time.Now().UnixNano()
		b := rc.buckets[userID]
		if b == nil {
			b = &tokenBucket{tokens: limit, lastNs: now}
			rc.buckets[userID] = b
		}
		elapsed := float64(now-b.lastNs) / 1e9
		b.lastNs = now
		b.tokens += elapsed * limit
		if b.tokens > limit { // 突发上限 = 1 秒额度
			b.tokens = limit
		}
		if b.tokens < float64(n) {
			return false // 超速，丢弃本包
		}
		b.tokens -= float64(n)
	}
	rc.bytes[userID] += uint64(n)
	return true
}
