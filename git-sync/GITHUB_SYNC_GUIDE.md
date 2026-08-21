# GitHub 上传教程（vulkan-blank / awa）

本文件教你如何把本项目推送到 GitHub 仓库 `oommgg123/AI`。
本地仓库已初始化（`main` 分支），并配好了远程 `origin` 与同步脚本 `git-sync/git_sync.sh`。
你只需完成「创建仓库 + 配置鉴权」两步，之后推送是自动/一键的。

---

## 第 1 步：在 GitHub 创建仓库 `AI`

1. 打开 https://github.com/new
2. **Repository name** 填：`AI`
3. **Visibility** 选 `Public`（公开，配合 MIT 协议）
4. **重要**：本仓库已含 `LICENSE` 与初始提交，**不要**勾选
   - ☐ Add a README file
   - ☐ Add .gitignore
   - ☐ Choose a license
   直接点 **Create repository**（创建空仓库即可，内容由我们 push 上去）。
5. 创建后页面会显示仓库地址，形如 `git@github.com:oommgg123/AI.git`（SSH）或
   `https://github.com/oommgg123/AI.git`（HTTPS）。本机远程已设为 SSH 形式。

---

## 第 2 步：配置推送鉴权（二选一）

GitHub 自 2021 年起不再接受「账号密码」推送，必须用 **SSH key** 或 **Personal Access Token (PAT)**。

### 方式 A：SSH Key（推荐，一次配置永久使用）

在本机 Git Bash 里执行：

```bash
# 1) 生成密钥（一路回车，不设密码最省事；已有可跳过）
ssh-keygen -t ed25519 -C "你的邮箱@example.com"

# 2) 启动 ssh-agent 并添加私钥
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# 3) 复制公钥内容（下面这条会打印，全选复制）
cat ~/.ssh/id_ed25519.pub
```

然后把复制的公钥粘到 GitHub：
- 右上角头像 → **Settings** → 左侧 **SSH and GPG keys** → **New SSH key**
- Title 随意（如 `my-pc`），Key type 选 `Authentication Key`，粘贴后 **Add SSH key**

验证：
```bash
ssh -T git@github.com
# 看到 "Hi oommgg123! You've successfully authenticated..." 即成功
```
（若提示 `Connection refused` 是网络/防火墙问题，需可访问 github.com 的环境。）

### 方式 B：Personal Access Token（HTTPS 方式）

1. GitHub → 头像 → **Settings** → **Developer settings** → **Personal access tokens** → **Tokens (classic)** → **Generate new token (classic)**
2. Note 填 `awa-sync`；Expiration 选合适时长；**勾选 `repo`**（完整仓库权限）
3. 生成后**立即复制**令牌（只显示一次）
4. 把本机远程改为 HTTPS 形式并写入令牌（二选一）：
   - 临时用环境变量（推荐，不落盘）：
     ```bash
     export GITHUB_TOKEN="ghp_xxxxxxxxxxxx"   # 你的令牌
     ```
   - 或永久改写远程 URL（令牌会留在 git 配置里，注意安全）：
     ```bash
     git remote set-url origin https://oommgg123:ghp_xxxxxxxxxxxx@github.com/oommgg123/AI.git
     ```

---

## 第 3 步：首次推送

在本机项目目录（`vulkan-blank`）的 Git Bash 中执行：

```bash
cd "C:/Users/Administrator/WorkBuddy/2026-08-16-23-13-11/vulkan-blank"
git push -u origin main
```

看到进度条即成功。此后去 https://github.com/oommgg123/AI 就能看到源码。

---

## 日常使用

### 一键同步（手动）
任何时候想上传，直接运行：
```bash
bash git-sync/git_sync.sh
```
脚本会：① 暂存全部源码改动 ② 把 `CMakeLists.txt` 的版本号补丁位 +1（如 `1.242.0` → `1.242.1`）③ 提交 ④ 推送。
若你用 Token 方式，先 `export GITHUB_TOKEN=...` 再运行。

### 自动同步
- **每 10 轮对话**：主代理会在满 10 轮代码改动后自动跑一次上面的脚本（计数器在 `git-sync/.sync_rounds`）。
- **每周兜底**：系统里建了一个「每周」自动任务（当前为暂停态），等你配好鉴权后，可在自动任务列表里把它启用；它也会跑同一个脚本。

---

## 排除项说明（`.gitignore`）
以下不会被上传，保持仓库精简：
- `build/` `build-debug/` `dist/` `优化版本/` `优化版本-Win7/`（编译产物/发布包）
- `资源/` `assets/`（贴图/DLL 等大资源，如需入库请删除对应忽略行）
- `.workbuddy/`（本地状态）
- `*.exe` `*.dll` `*.obj` 等二进制

若想连资源一起传，编辑 `.gitignore` 删掉对应行再提交即可。

## 协议
仓库已带 `LICENSE`（MIT，著作人「大章鱼用ai做的」），上传即开源。
