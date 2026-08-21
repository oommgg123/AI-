#!/usr/bin/env bash
# git_sync.sh —— 自动同步到 GitHub：提交全部改动 + 自增补丁版本号 + 推送
# 用法： bash tools/git_sync.sh
# 依赖：git 已在 PATH；远程 origin 已配置（或设置环境变量 GIT_REMOTE / GITHUB_TOKEN）
set -u

# 切到仓库根（脚本位于 <root>/tools/）
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || { echo "[git_sync] 无法进入仓库根目录"; exit 1; }

# 若未配置 origin 但给了 GIT_REMOTE，则自动添加
if ! git remote get-url origin >/dev/null 2>&1; then
  if [ -n "${GIT_REMOTE:-}" ]; then
    git remote add origin "$GIT_REMOTE"
    echo "[git_sync] 已添加远程 origin -> $GIT_REMOTE"
  else
    echo "[git_sync] 未配置 origin，也未设置 GIT_REMOTE 环境变量，仅做本地提交。"
  fi
fi

# 若使用 https 且提供 GITHUB_TOKEN，则临时用 token 改写 push URL（不落盘）
USE_TOKEN=0
if [ -n "${GITHUB_TOKEN:-}" ] && git remote get-url origin 2>/dev/null | grep -q '^https://'; then
  ORIG_URL="$(git remote get-url origin)"
  TOKEN_URL="$(echo "$ORIG_URL" | sed -E "s#^(https://)(.*)#\1x-access-token:${GITHUB_TOKEN}@\2#")"
  git remote set-url origin "$TOKEN_URL"
  USE_TOKEN=1
  echo "[git_sync] 已用 GITHUB_TOKEN 临时改写远程 URL 用于推送"
fi

# 自增补丁版本号： project(vulkan-blank VERSION 1.242.0 LANGUAGES CXX)
if [ -f CMakeLists.txt ]; then
  LINE="$(grep -nE 'project\(vulkan-blank VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | head -1)"
  if [ -n "$LINE" ]; then
    OLD="$(echo "$LINE" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    MAJ="$(echo "$OLD" | cut -d. -f1)"
    MIN="$(echo "$OLD" | cut -d. -f2)"
    PAT="$(echo "$OLD" | cut -d. -f3)"
    NEWPAT=$((PAT + 1))
    NEW="${MAJ}.${MIN}.${NEWPAT}"
    sed -i -E "s/(project\(vulkan-blank VERSION )${MAJ}\.${MIN}\.${PAT}( LANGUAGES CXX\))/\1${NEW}\2/" CMakeLists.txt
    echo "[git_sync] 版本号自增: ${OLD} -> ${NEW}"
    VERSION="$NEW"
  else
    VERSION="unknown"
  fi
else
  VERSION="unknown"
fi

# 暂存全部（遵循 .gitignore）
git add -A

# 无改动则跳过提交
if git diff --cached --quiet; then
  echo "[git_sync] 无改动，跳过提交。"
else
  MSG="chore: auto-sync v${VERSION} ($(date +%Y-%m-%d_%H:%M))"
  git commit -m "$MSG" >/dev/null && echo "[git_sync] 已提交: $MSG"
fi

# 推送（若已配置 origin）
if git remote get-url origin >/dev/null 2>&1; then
  BRANCH="$(git branch --show-current 2>/dev/null)"
  if [ -z "$BRANCH" ]; then
    BRANCH="main"
    git checkout -b "$BRANCH" 2>/dev/null || BRANCH="$(git branch --show-current)"
  fi
  if git push --set-upstream origin "$BRANCH" 2>&1; then
    echo "[git_sync] 已推送到 origin/$BRANCH"
  else
    echo "[git_sync] 推送失败（检查鉴权/网络/仓库是否存在）。本地提交已保留。"
  fi
else
  echo "[git_sync] 无远程，已仅完成本地提交。"
fi

# 还原 token URL，避免凭据残留在配置中
if [ "$USE_TOKEN" = "1" ] && [ -n "${ORIG_URL:-}" ]; then
  git remote set-url origin "$ORIG_URL"
fi

echo "[git_sync] 完成。"
