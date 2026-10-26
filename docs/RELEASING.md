# 发布流程

## 版本约定

根目录 `VERSION` 是唯一版本来源，内容为稳定版本 `MAJOR.MINOR.PATCH`，Git 标签使用 `vX.Y.Z`。
CMake 自动生成程序版本头文件、Windows VERSIONINFO 和应用清单；构建脚本从同一文件确定 ZIP 名称。
发布前把对应条目加入 `CHANGELOG.md`。当前自动发布与更新检查只支持正式版本，不发布 `-beta` 等预发行标签。

## 发布一个版本

1. 在变更分支完成修改，更新 `VERSION` 和 `CHANGELOG.md`，通过 PR 合入 `main`。
2. 确认 `Build` 工作流成功；交互相关内容由维护者在真实设备验证。
3. 将本地 `main` 更新到准备发布的提交，确认工作区干净。
4. 使用 SSH 或签名前执行 `ssh-add -l`。1Password 代理未启动时停止，不切换认证方式绕过。
5. 执行：

```powershell
$releaseVersion = (Get-Content VERSION -Raw).Trim()
git tag -a "v$releaseVersion" -m "Imago v$releaseVersion"
git push origin "v$releaseVersion"
```

`Release` 工作流会校验标签、版本和更新日志，在 Windows 2022 runner 构建 x64 Release、运行无界面测试、验证 EXE 版本并生成 ZIP 与 SHA-256。
随后创建草稿、上传资产、核对校验值，全部成功才公开 Release 并标记为 latest。
软件更新检查使用这一正式 Release 的版本和已上传 ZIP 元数据，不依赖仓库提交或 CI 临时产物。

## 工作流与权限

- `ci.yml`：`main` push、PR 或手动触发；只读仓库权限，保留构建产物 14 天。
- `release.yml`：`v*.*.*` 标签 push；也可针对已有标签手动运行，分支运行不会发布。
- 构建 job 只读；发布 job 单独获得 `contents: write`。使用 GitHub 内置 `GITHUB_TOKEN`，无需额外 PAT。
- GitHub Actions 以不可变 SHA 固定，并由 Dependabot 定期提议更新；不自动合并。
- 发布只打包当前工作流产生的 ZIP 和校验文件，不上传本机历史 `dist/`。

## 失败处理

若构建、测试或资产上传失败，检查 Actions 日志，修复后按情况重跑工作流。未完成的草稿可以继续上传。
已公开版本不覆盖文件、不移动标签；代码问题发布新的补丁版本。
若失败需要修改已打标签的代码，保留原标签，增加版本号重新发布。

目前没有 Authenticode 签名，勿宣称发行包已经代码签名。SHA-256 用于下载完整性核对。
本机包与 Actions 包可能因工具链、SDK 与时间戳不同而有不同哈希，公开 Release 以 Actions 产物为准。
