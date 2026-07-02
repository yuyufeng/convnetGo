package main

// adminserver.go —— 服务器管理后台（stdlib net/http，独立端口，HTTP Basic Auth）。
// 功能：查看用户/在线状态/已中继流量；开关每个用户的服务器中继；设每用户 + 全局中继限速。

import (
	"encoding/json"
	"net/http"
	"strconv"

	"github.com/labstack/gommon/log"
)

// StartAdminServer 在独立 goroutine 启动管理后台。用户名固定 admin，密码为 adminPass。
func StartAdminServer(port, adminPass string) {
	mux := http.NewServeMux()
	mux.HandleFunc("/", withAuth(adminPass, adminIndex))
	mux.HandleFunc("/api/users", withAuth(adminPass, apiUsers))
	mux.HandleFunc("/api/user/relay", withAuth(adminPass, apiSetUserRelay))
	mux.HandleFunc("/api/settings", withAuth(adminPass, apiSettings))
	go func() {
		log.Info("管理后台已启动: http://0.0.0.0:" + port + " (用户名 admin)")
		if err := http.ListenAndServe("0.0.0.0:"+port, mux); err != nil {
			log.Error("管理后台启动失败:", err)
		}
	}()
}

func withAuth(pass string, h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		_, p, ok := r.BasicAuth()
		if !ok || p != pass {
			w.Header().Set("WWW-Authenticate", `Basic realm="convnet-admin"`)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		h(w, r)
	}
}

type adminUserRow struct {
	UserID         uint64 `json:"userID"`
	Account        string `json:"account"`
	Nick           string `json:"nick"`
	CvnIP          string `json:"cvnIP"`
	Online         bool   `json:"online"`
	RelayBlocked   bool   `json:"relayBlocked"`
	RelayLimitKBps int    `json:"relayLimitKBps"`
	RelayBytes     uint64 `json:"relayBytes"`
}

func apiUsers(w http.ResponseWriter, r *http.Request) {
	if store == nil {
		http.Error(w, "store 未初始化", http.StatusInternalServerError)
		return
	}
	rows := []adminUserRow{}
	for _, u := range store.AllUsers() {
		var rb uint64
		if relayCtl != nil {
			rb = relayCtl.TotalBytes(u.UserID)
		}
		rows = append(rows, adminUserRow{
			UserID: u.UserID, Account: u.Account, Nick: u.Nick, CvnIP: u.CvnIP,
			Online: isOnline(u.UserID), RelayBlocked: u.RelayBlocked,
			RelayLimitKBps: u.RelayLimitKBps, RelayBytes: rb,
		})
	}
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"users":           rows,
		"globalRelayKBps": store.GlobalRelayLimit(),
	})
}

func apiSetUserRelay(w http.ResponseWriter, r *http.Request) {
	if store == nil {
		http.Error(w, "store 未初始化", http.StatusInternalServerError)
		return
	}
	userID, _ := strconv.ParseUint(r.FormValue("userID"), 10, 64)
	if userID == 0 {
		http.Error(w, "userID 无效", http.StatusBadRequest)
		return
	}
	blocked := r.FormValue("blocked") == "1" || r.FormValue("blocked") == "true"
	limit, _ := strconv.Atoi(r.FormValue("limitKBps"))
	if err := store.SetUserRelay(userID, blocked, limit); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if relayCtl != nil {
		relayCtl.SetUser(userID, blocked, limit)
	}
	w.Write([]byte("ok"))
}

func apiSettings(w http.ResponseWriter, r *http.Request) {
	if store == nil {
		http.Error(w, "store 未初始化", http.StatusInternalServerError)
		return
	}
	kbps, _ := strconv.Atoi(r.FormValue("globalRelayKBps"))
	store.SetGlobalRelayLimit(kbps)
	if relayCtl != nil {
		relayCtl.SetGlobal(kbps)
	}
	w.Write([]byte("ok"))
}

func adminIndex(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write([]byte(adminHTML))
}

const adminHTML = `<!doctype html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ConvnetGo 管理后台</title>
<style>
 body{font-family:system-ui,"Microsoft YaHei",sans-serif;margin:24px;color:#222}
 h1{font-size:20px} h2{font-size:15px;color:#555;margin-top:24px}
 table{border-collapse:collapse;width:100%;margin-top:8px}
 th,td{border:1px solid #ddd;padding:6px 8px;text-align:left;font-size:13px}
 th{background:#f5f5f5}
 .on{color:#1a7f37;font-weight:bold} .off{color:#8c959f}
 button{padding:3px 10px;cursor:pointer} input[type=number]{width:90px}
 .bar{margin:12px 0;padding:10px;background:#f8f9fa;border:1px solid #eee;border-radius:6px}
 .muted{color:#888;font-size:12px}
</style></head>
<body>
<h1>ConvnetGo 服务器管理后台</h1>
<div class="bar">
  全局中继限速：<input type="number" id="glimit" min="0" value="0"> KB/s（0=不限）
  <button onclick="saveGlobal()">保存全局限速</button>
  <button onclick="load()">刷新</button>
  <span class="muted">中继=P2P 失败时经服务器转发的通道</span>
</div>
<h2>用户列表</h2>
<table id="tbl"><thead><tr>
 <th>用户ID</th><th>账号</th><th>昵称</th><th>虚拟IP</th><th>在线</th>
 <th>允许中继</th><th>限速(KB/s,0=不限)</th><th>已中继</th><th>操作</th>
</tr></thead><tbody></tbody></table>
<p class="muted">说明：关闭"允许中继"后该用户 P2P 失败时无法经服务器转发（IM 聊天不受影响，走另一条通道）。限速为空/0 时回退全局限速。</p>
<script>
function human(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';if(n<1073741824)return (n/1048576).toFixed(1)+' MB';return (n/1073741824).toFixed(2)+' GB';}
async function load(){
 const r=await fetch('/api/users'); const d=await r.json();
 document.getElementById('glimit').value=d.globalRelayKBps||0;
 const tb=document.querySelector('#tbl tbody'); tb.innerHTML='';
 (d.users||[]).forEach(u=>{
  const tr=document.createElement('tr');
  tr.innerHTML=
   '<td>'+u.userID+'</td><td>'+(u.account||'')+'</td><td>'+(u.nick||'')+'</td><td>'+(u.cvnIP||'')+'</td>'+
   '<td class="'+(u.online?'on':'off')+'">'+(u.online?'在线':'离线')+'</td>'+
   '<td><input type="checkbox" '+(u.relayBlocked?'':'checked')+' id="a'+u.userID+'"></td>'+
   '<td><input type="number" min="0" value="'+(u.relayLimitKBps||0)+'" id="l'+u.userID+'"></td>'+
   '<td>'+human(u.relayBytes||0)+'</td>'+
   '<td><button onclick="saveUser('+u.userID+')">保存</button></td>';
  tb.appendChild(tr);
 });
}
async function saveUser(id){
 const allowed=document.getElementById('a'+id).checked;
 const limit=document.getElementById('l'+id).value||0;
 const body=new URLSearchParams({userID:id, blocked:allowed?'0':'1', limitKBps:limit});
 await fetch('/api/user/relay',{method:'POST',body});
 load();
}
async function saveGlobal(){
 const v=document.getElementById('glimit').value||0;
 await fetch('/api/settings',{method:'POST',body:new URLSearchParams({globalRelayKBps:v})});
 load();
}
load();
</script>
</body></html>`
