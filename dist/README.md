# ConvnetGo 发布二进制（dist/）

对应分支 `CONVNETQT`，均由该分支源码构建（2026-07-02）。

## 客户端（Windows x64）
- **`convnet-qt-windows-x64.zip`** —— Qt6 IM 客户端，已用 `windeployqt` 打包好所需的 Qt DLL 与插件
  （`platforms/`、`styles/`、`tls/`、`imageformats/`、`translations/` 等），并附 `datachannel.dll`/`juice.dll`/
  `libssl`/`libcrypto`/`wintun.dll`。
  - **用法**：解压后直接运行 `convnet-qt.exe`。
  - **依赖**：需要 *Microsoft Visual C++ 2015–2022 Redistributable (x64)*（未内置）。若提示缺少
    `VCRUNTIME140.dll`，安装：https://aka.ms/vs/17/release/vc_redist.x64.exe
  - **虚拟网卡**：`wintun.dll` 已随包；启用虚拟网卡需**以管理员身份**运行。
  - 登录：账号 + 密码（首次可在登录框注册）。服务器地址默认连配置的信令服务器。

## 服务端
- **`convnetgo-server-windows-amd64.exe`** —— Windows x64
- **`convnetgo-server-linux-amd64`** —— Linux x86-64（静态，无 libc 依赖）
- **`convnetgo-server-linux-arm64`** —— Linux ARM64（静态）
  - **用法**：与 `convnet.json` 放同一目录，运行 `<binary> -s`。监听 TCP 13903（信令）、UDP 13902（TURN）、
    TCP 8099（管理后台）。
  - Linux 先 `chmod +x`。完整的编译/配置/systemd/Docker/防火墙/排错见仓库根 **`DEPLOY-LINUX.md`**。

---
> 这些二进制随分支入库只是为了方便分发；更规范的方式是发布到 GitHub Release（需要 `gh` CLI 或
> Personal Access Token）。若需要，我可以在你提供其一后改用 Release。
