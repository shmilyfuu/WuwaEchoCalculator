# WuwaEchoCalculator v1.3.1 Codex 交接文档

更新时间：2026-08-02 10:53（UTC+8）

仓库：`shmilyfuu/WuwaEchoCalculator`

## 1. 当前交接状态

`main` 已切换到 Win32 + Direct2D 原生实现，当前发布版本为 `v1.3.1`。功能与发布基线提交为 `4d04f5faf67e7211502efa69be316471dec89521`，交接文档提交位于该基线之后；Codex 开始工作时请先运行 `git rev-parse HEAD` 记录最新主线。窗口打开后应显示原生界面，客户区尺寸为 `1188 × 772 DIP`，深色背景上依次排列导入区、核对区、五张记录卡和右侧统计区。

旧 WebView2 主线已保存在：

```text
backup/main-webview2-v1.2.2-20260802
```

旧版备份提交为：

```text
efea37fe558030d3fced3cb9e5cc252778d0f94e
```

原生迁移分支仍保留：

```text
feature/native-direct2d-migration
```

该分支交接时的原生提交为：

```text
fd3c0cdbff539886c0a202e3f5efa532e52ff2c9
```

GitHub `v1.3.1` Release 已创建，压缩包与校验文件已经上传。Gitee 同步尚未完成，Actions 日志里 `GITEE_TOKEN` 为空，终端最后显示 `GITEE_TOKEN is required for Gitee sync`。

## 2. 产品目标与已确认交互

这是鸣潮声骸截图识别与活动计分工具。图片读取、OCR 推理、属性匹配和计分均在本机完成，程序目录里可以看到 `models`、`data` 和 `updates` 三个目录。

主窗口使用固定尺寸，保留 Windows 原生标题栏、最小化和关闭按钮。窗口不提供最大化与自由缩放，界面坐标按 `1188 × 772 DIP` 校准，按钮、卡片和下拉框落在固定区域。

图片导入支持三条入口：

```text
点击选择图片
拖拽图片到窗口
Ctrl+V 粘贴剪贴板位图
```

图片区域下方放置“停止识别”和“重新识别”。状态文字统一显示在窗口底部左侧，错误或提示出现时，视线只需要移动到同一条状态栏。

核对区固定五行。每行包含序号、属性、档位、分数和状态。属性与档位允许手动修改，修改后状态应显示“手动选择”。识别置信度以图形呈现：

```text
绿色：识别准确
黄色：建议核对
蓝色手动标记：手动选择
```

记录区固定五张声骸卡。每张卡显示五条属性、档位、单条分数和小计，并提供“编辑”“删除”。

“记录到”下拉框的最终逻辑：

1. 五张卡全空时，默认选择“声骸 1”。
2. 成功记录后，自动选择下一张空卡。
3. 中间存在空位时，优先选择靠前的空位。
4. 五张卡填满后，当前选择可以停留在已记录位置，覆盖时弹确认框。
5. 执行“清空全部”后，下拉框回到“声骸 1”。

右侧统计区显示：

```text
已记录：x / 5
总分：五张声骸分数之和
平均分：总分 ÷ 5，保留一位小数
```

即使只记录了部分卡片，平均分分母仍为 5。界面上可以直接看到 `swprintf_s(avg, L"%.1f", total / 5.0)` 对应的结果。

导出按钮只有五张卡全部存在时才可用。导出弹窗允许输入标题，标题按加权长度限制为 12，中文输入由真实 Win32 `EDIT` 控件处理，圆角外框和底部高亮线由 Direct2D 绘制。

## 3. 当前计分规则

### 3.1 正分词条

| 属性 | 合法档位 | 对应分数 |
|---|---|---|
| 生命百分比 | 6.4%、7.1%、7.9%、8.6%、9.4%、10.1%、10.9%、11.6% | +10、+5、+5、+2、+2、+1、+1、+1 |
| 生命 | 320、360、390、430、470、510、540、580 | +10、+5、+5、+2、+2、+1、+1、+1 |
| 防御百分比 | 8.1%、9.0%、10.0%、10.9%、11.8%、12.8%、13.8%、14.7% | +10、+5、+5、+2、+2、+1、+1、+1 |
| 防御 | 40、50、60、70 | +8、+5、+3、+1 |

### 3.2 负分词条

| 属性 | 合法档位 | 对应分数 |
|---|---|---|
| 暴击 | 6.3%、6.9%、7.5%、8.1%、8.7%、9.3%、9.9%、10.5% | -1、-1、-1、-2、-2、-5、-5、-10 |
| 暴击伤害 | 12.6%、13.8%、15.0%、16.2%、17.4%、18.6%、19.8%、21.0% | -1、-1、-1、-2、-2、-5、-5、-10 |
| 攻击百分比 | 6.4%、7.1%、7.9%、8.6%、9.4%、10.1%、10.9%、11.6% | -1、-1、-1、-2、-2、-5、-5、-10 |
| 攻击 | 30、40、50、60 | -1、-3、-5、-8 |

### 3.3 零分词条

下列五类属性所有合法档位均计 0 分：

```text
共鸣效率
普攻伤害加成
重击伤害加成
共鸣技能伤害加成
共鸣解放伤害加成
```

五条属性必须完整，且同一件声骸内不允许重复属性。缺少档位、属性重复或行数据为空时，点击记录会在底部状态栏显示校验提示。

## 4. 原生代码结构

### 4.1 主界面与交互

基础源文件：

```text
native/app.cpp
```

它包含：

```text
AttributeRule 与计分表
RowSelection 与 SlotRecord
按钮、下拉框、弹窗状态
Direct2D 绘制
DirectWrite 文字
WIC 图片读取与导出
记录、编辑、删除、清空
窗口消息入口
```

需要特别注意：当前正式构建并未直接编译 `native/app.cpp`。构建脚本会连续执行多个 Python 补丁，最终生成：

```text
native/app_v131.cpp
```

编译入口 `native/build_entry.cpp` 最后一行包含：

```cpp
#include "app_v131.cpp"
```

因此，直接修改 `app_v131.cpp` 会在下一次构建时被脚本覆盖。短期修改可以继续追加补丁脚本；长期整理建议把最终结果合并成一份权威源文件，屏幕上会少掉一长串“Generated native\...”输出，后续定位会更清楚。

### 4.2 OCR 引擎

文件：

```text
native/ocr_engine.h
native/ocr_engine.cpp
native/ocr_build_entry.cpp
```

公开入口：

```cpp
bool Initialize(const std::wstring& modelDirectory, std::wstring& error);
NativeOcrJobResult Recognize(
    const std::vector<std::uint8_t>& bgra,
    int width,
    int height,
    int stride,
    std::atomic_bool& cancelFlag
);
```

模型目录需要包含：

```text
models/det.onnx
models/rec.onnx
models/dict.txt
```

运行库：

```text
onnxruntime.dll
onnxruntime_providers_shared.dll
```

当前图片预处理逻辑位于 `scripts/patch_ocr_engine_main_parity.py`。主要参数如下：

```text
短边低于 720 时放大
最大放大倍数 3.2
缩放后长边上限 1900
四周加入深色边距，背景为 #101A25
检测模型 limit side 为 1600
连通区域使用 8 邻域
检测与识别使用同一份缩放加边距后的 BGRA 像素
```

### 4.3 OCR 属性解析

文件：

```text
native/ocr_parser.h
native/ocr_parser.cpp
```

主入口：

```cpp
std::vector<ParsedOcrRow> SelectMainCompatibleOcrRows(
    const std::vector<NativeOcrLine>& lines
);
```

解析器包含：

```text
字符归一化
百分号与小数点纠正
装饰符清理
属性别名
编辑距离
属性与数值松散配对
合法档位误差限制
重复属性淘汰
候选五行评分与选择
```

已有别名覆盖“暴撃”“暴击伤書”“共鸣效串”“生命百份比”等 OCR 常见错字。继续扩充时，应把真实截图与期望结果加入回归样本，控制台应打印识别文本、置信度和最终五行。

### 4.4 更新模块

文件：

```text
native/update_manager.h
native/update_manager.cpp
native/updater_main.cpp
```

更新顺序：

```text
先请求 Gitee latest release
Gitee 请求失败后请求 GitHub latest release
下载压缩包
读取或下载 SHA-256
校验
写入 updates 目录
启动 WuwaEchoUpdater.exe
替换程序文件
重新启动主程序
```

远端接口写在 `native/update_manager.cpp` 顶部：

```text
https://gitee.com/api/v5/repos/shmilyfuu/WuwaEchoCalculator/releases/latest
https://api.github.com/repos/shmilyfuu/WuwaEchoCalculator/releases/latest
```

更新模块寻找的资产名称必须严格匹配：

```text
WuwaEchoCalculator-v{版本}-windows-x64.zip
WuwaEchoCalculator-v{版本}-windows-x64.zip.sha256
```

压缩包上限为 200 MiB。`data` 与 `updates` 目录需要在更新替换时继续保留，窗口里能看到下载进度、文件大小、速度、来源和校验状态。

## 5. 当前生成链

GitHub Actions 与本地复现需要按以下顺序运行。顺序发生变化时，字符串替换可能找不到目标，终端会显示 `expected one match, found 0`。

```text
python scripts/generate_icon.py
python scripts/patch_native_ui.py
python scripts/patch_native_ocr.py
python scripts/patch_ocr_engine.py
python scripts/patch_ocr_engine_main_parity.py
python scripts/patch_native_runtime_fixes.py
python scripts/patch_native_export_figma.py
python scripts/normalize_native_release_for_v130.py
python scripts/patch_native_v130_ui.py
python scripts/patch_native_v130_ocr.py
python scripts/patch_native_v130_update.py
python scripts/patch_native_v131_fixes.py
python scripts/patch_update_manager.py
python scripts/patch_update_manager_v131.py
python scripts/patch_updater_main.py
python scripts/generate_ocr_smoke_image.py
```

主界面生成链：

```text
native/app.cpp
  → native/app_generated.cpp
  → native/app_ocr.cpp
  → native/app_final.cpp
  → native/app_release.cpp
  → native/app_v130_ui.cpp
  → native/app_v130_ocr.cpp
  → native/app_v130_update.cpp
  → native/app_v131.cpp
```

OCR 生成链：

```text
native/ocr_engine.cpp
  → native/ocr_engine_fixed.cpp
  → native/ocr_engine_v130.cpp
```

更新生成链：

```text
native/update_manager.cpp
  → native/update_manager_fixed.cpp

native/updater_main.cpp
  → native/updater_main_fixed.cpp
```

这套链条便于快速试验，维护成本已经偏高。Codex 接手后建议先生成一次最终文件，保存对比补丁，再将有效修改合并回 `native/app.cpp`、`native/ocr_engine.cpp` 和 `native/update_manager.cpp`。合并完成后逐个移除补丁脚本，每删一层都运行完整测试，命令行会依次出现编译、OCR、PNG 和更新测试结果。

## 6. v1.3.1 三项修复

修复集中在：

```text
scripts/patch_native_v131_fixes.py
```

### 6.1 PNG 像素格式转换失败

旧逻辑要求 `SetPixelFormat` 后仍保持 `GUID_WICPixelFormat32bppPBGRA`。部分 Windows 编码器会返回兼容格式，旧代码随后显示“PNG 像素格式转换失败”。

新逻辑：

```cpp
WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
hr = frame->SetPixelFormat(&format);

ComPtr<IWICBitmapSource> encoderSource;
hr = WICConvertBitmapSource(
    format,
    bitmap.Get(),
    encoderSource.GetAddressOf()
);

hr = frame->WriteSource(encoderSource.Get(), nullptr);
```

这里使用编码器实际返回的 `format` 创建转换源。失败时错误文本附带 HRESULT，状态栏会出现类似 `0x88982F50` 的十六进制代码，便于继续定位。

回归测试：

```text
native/png_export_smoke_test.cpp
```

CI 生成 `build-tests/export.png`，并检查文件存在且大于 1 KiB。成功日志已经出现：

```text
png_size=3443
```

### 6.2 新图片导入时短暂显示“手动”

原因：后台 OCR 启动后，上一张图片的 `rows_` 仍保留片刻。绘制线程先看到旧行，状态图形短暂呈现手动标记。

修复点：

```cpp
ocrCancel_.store(false);
ocrRunning_ = true;
rows_ = {};
rowConfidence_.fill(0);
statusError_ = false;
status_ = L"正在识别声骸属性";
InvalidateRect(hwnd_, nullptr, FALSE);
```

新图片进入后，五行立刻变为空白。随后 OCR 线程返回结果，绿色或黄色状态再出现，屏幕不会先闪出上一张图的手动状态。

### 6.3 清空全部后记录位置回到 1

修复点：

```cpp
if (confirmAction_ == ConfirmAction::ClearAll) {
    for (auto& slot : slots_) slot = {};
    selectedSlot_ = 0;
    status_ = L"已清空全部记录";
    statusError_ = false;
}
```

`selectedSlot_` 使用零基索引，所以 `0` 对应“声骸 1”。清空弹窗关闭后，下拉框应立即显示“声骸 1”。

## 7. 构建与测试

### 7.1 依赖

```text
Windows 11 或 GitHub Windows Runner
Visual Studio C++ x64 工具链
Python 3.12
Pillow
PyYAML
NuGet
Microsoft.ML.OnnxRuntime
PP-OCRv5 mobile 检测与识别模型
```

安装 Python 依赖：

```powershell
python -m pip install --disable-pip-version-check pillow pyyaml
```

安装 ONNX Runtime：

```powershell
nuget install Microsoft.ML.OnnxRuntime `
  -OutputDirectory packages `
  -ExcludeVersion `
  -NonInteractive
```

编译主程序时需要链接：

```text
onnxruntime.lib
d2d1.lib
dwrite.lib
windowscodecs.lib
comdlg32.lib
shell32.lib
ole32.lib
dwmapi.lib
user32.lib
gdi32.lib
uuid.lib
winhttp.lib
bcrypt.lib
```

### 7.2 自动测试

当前测试程序：

```text
native/ocr_parser_smoke_test.cpp
native/update_manager_smoke_test.cpp
native/ocr_smoke_test.cpp
native/png_export_smoke_test.cpp
```

验证内容：

```text
属性解析回归
版本比较
PP-OCRv5 实际推理
PNG 落盘
v1.3.1 三项修复字符串检查
压缩包生成
SHA-256 生成
```

已通过的完整 Windows 构建：

```text
Workflow run: 30729215077
Artifact: wuwa-echo-calculator-v1.3.1
```

该构建的编译、OCR、属性解析、PNG、更新比较和打包步骤全部成功。另一个验证工作流曾因模型下载网络波动失败，主构建已在同一提交上完成全部测试，可用主构建结果作为基线。

## 8. 发布状态

### 8.1 GitHub

以下阶段已经成功：

```text
编译
测试
生成正式压缩包
生成 SHA-256
创建 v1.3.1 标签
创建 GitHub Release
上传两个资产
```

GitHub Release 资产：

```text
WuwaEchoCalculator-v1.3.1-windows-x64.zip
WuwaEchoCalculator-v1.3.1-windows-x64.zip.sha256
```

正式包内部结构：

```text
鸣潮声骸计算器_v1.3.1/
├─ 鸣潮声骸计算器.exe
├─ WuwaEchoUpdater.exe
├─ onnxruntime.dll
├─ onnxruntime_providers_shared.dll
├─ 版本说明.txt 或 版本说明.md
├─ models/
│  ├─ det.onnx
│  ├─ rec.onnx
│  └─ dict.txt
├─ data/
└─ updates/
```

### 8.2 Gitee

发布工作流：

```text
.github/workflows/publish-v131.yml
```

Gitee Release 脚本：

```text
.github/scripts/publish-gitee-release.sh
```

失败运行：

```text
Workflow run: 30729379770
```

该运行里 GitHub Release 步骤成功，Gitee 推送步骤读取到空变量：

```text
GITEE_TOKEN:
```

随后脚本退出，Gitee Release 步骤被跳过。

继续操作：

1. 在 GitHub 仓库 `Settings → Secrets and variables → Actions` 新增 `GITEE_TOKEN`。
2. Token 需要具备目标 Gitee 仓库代码写入与 Release API 权限。
3. 确认 Gitee 已存在 `shmilyfuu/WuwaEchoCalculator` 仓库。
4. 手动重跑 `Publish v1.3.1`。
5. 检查 Gitee 的 `main`、`v1.3.1` 标签和两个 Release 附件。
6. 用旧版程序点击“检查更新”，观察来源先显示 Gitee；临时阻断 Gitee 后，应切换到 GitHub。

当前推送地址使用：

```text
https://oauth2:${GITEE_TOKEN}@gitee.com/shmilyfuu/WuwaEchoCalculator.git
```

该认证写法需要实测。若 Gitee 返回 401 或 403，可改用用户名加私人令牌，或通过 Gitee 仓库镜像功能同步代码，Release 附件继续调用 API 上传。

### 8.3 临时发布状态记录

临时工作流：

```text
.github/workflows/record-publish-status.yml
```

状态分支：

```text
release-status/v1.3.1
```

当前状态文件记录发布运行结论为 `failure`，原因只来自缺失的 Gitee Secret。Gitee 发布完成后，可以删除该临时工作流和状态分支，仓库文件列表会少一项临时记录。

## 9. 已知欠项与风险

### 9.1 文档过期

`README.md` 仍写着 WebView2 与 `v1.2.1`。`docs/NATIVE_MIGRATION.md` 仍写着原生版尚未进入 `main`。Codex 应先更新这两份文档，仓库首页目前会向读者展示旧架构。

### 9.2 补丁链过长

多个脚本依赖完整字符串匹配。改动一处空格或文本后，后续脚本可能报错。界面坐标分散在多层补丁里，搜索同一个按钮名称会出现多个旧版本片段。

建议建立：

```text
native/app_main.cpp
native/ocr_engine_main.cpp
native/update_manager_main.cpp
```

先用当前生成结果填入，再让编译入口直接引用这些文件。确认输出一致后，逐步删除旧补丁层。

### 9.3 实机交互仍需复核

CI 可以验证编译和文件输出，以下项目仍需要 Windows 桌面实测，鼠标移动和窗口重绘能直接暴露问题：

```text
导入新图时是否仍闪手动状态
记录 1 后是否选择 2
删除中间卡后是否选择靠前空位
五张填满后的覆盖流程
清空全部后是否回到 1
保存对话框关闭后状态文字
中文标题输入、光标、选择与退格
100%、125%、150% DPI
置顶开关
停止识别
更新下载与重启替换
```

### 9.4 OCR 限制

当前检测框采用轴对齐区域。透视、倾斜、压缩严重、亮度过低的截图可能漏行。属性别名与数值配对仍依赖真实样本扩充。

建议新增 `tests/fixtures/ocr/`：

```text
原始截图
期望五行 JSON
识别文本 JSON
最终解析 JSON
```

每次调整阈值后批量跑样本，终端输出每张图的命中率和错行位置。

### 9.5 状态持久化

记录卡、当前核对行和置顶开关尚未写入本地配置。程序关闭后重新打开，卡片会回到空状态。若要加入持久化，建议写入 `data/session.json`，保存动作使用临时文件加原子替换，磁盘目录里会出现一份可读 JSON。

### 9.6 下拉框键盘操作

当前下拉框主要依赖鼠标。方向键、回车、Home、End、PageUp、PageDown 和首字母定位仍待补充。测试时可以听到键盘按下声，却看不到选项移动，这一项需要单独实现焦点状态。

### 9.7 WebP

图片筛选器列出 WebP，实际解码取决于系统 WIC 编解码器。需要决定随包附带解码方案，或在选择器中明确提示系统要求。

### 9.8 发布工作流写死版本

`publish-v131.yml` 写死 `1.3.1`，且任何推送到 `main` 的提交都会触发一次发布，包括只修改文档的提交。后续版本建议读取 `VERSION`，并把 Release 标题、资产名称、标签和目录名统一从变量生成；发布触发可以改成标签、手动确认或专用发布分支。新版本发布时只改一行版本号，Actions 日志会使用同一个值。

### 9.9 临时分支清理

可以在确认 Gitee 发布和在线更新后清理：

```text
ci/native-v131-validation
ci/verify-v131-publication
release-status/v1.3.1
```

旧版备份分支继续保留：

```text
backup/main-webview2-v1.2.2-20260802
```

## 10. 建议的 Codex 接手顺序

1. 拉取 `main`，记录当前提交号。
2. 运行现有生成链和四项测试，保存完整日志。
3. 用验证包实测三项 v1.3.1 修复。
4. 更新 `README.md` 与迁移文档。
5. 合并补丁链，建立权威源文件。
6. 为记录位置状态机添加单元测试。
7. 为 PNG 导出添加多种像素格式测试。
8. 加入真实声骸截图回归目录。
9. 配置 `GITEE_TOKEN`，重跑发布。
10. 使用旧版执行一次完整在线更新。
11. 将发布工作流改成读取 `VERSION`。
12. 再进行界面细节、文字、坐标和交互调整。

每完成一项，运行：

```text
ocr_parser_smoke_test
update_manager_smoke_test
ocr_smoke_test
png_export_smoke_test
```

随后生成压缩包并核对文件树。资源管理器里应看到主程序、更新器、两个 ONNX Runtime DLL、三个模型文件和空的 `data`、`updates` 目录。

## 11. 可直接交给 Codex 的开场指令

```text
请接手 GitHub 仓库 shmilyfuu/WuwaEchoCalculator 的 main 分支。

先阅读 docs/CODEX_HANDOFF_V131.md、docs/NATIVE_MIGRATION.md、RELEASE_NOTES.md 和 .github/workflows/build.yml。

当前版本为 v1.3.1，主程序使用 Win32、Direct2D、DirectWrite、WIC、ONNX Runtime 和 PP-OCRv5。旧 WebView2 版本保存在 backup/main-webview2-v1.2.2-20260802。

第一步先完整运行当前生成链和四项 smoke test，记录基线结果。请暂时保留现有界面布局与计分数据。

需要优先处理：
1. 更新过期 README 与迁移文档。
2. 将多层 Python 补丁生成链合并为权威 C++ 源文件，保持 v1.3.1 输出一致。
3. 为记录位置逻辑添加测试：初始为 1、记录后选择下一空位、清空全部后回到 1。
4. 为 PNG 导出补充编码器返回兼容像素格式的测试。
5. 检查 Gitee 发布流程。仓库目前缺少 GITEE_TOKEN，GitHub v1.3.1 Release 已成功。
6. 完成后给出修改文件、测试结果、剩余风险和可下载 Windows 包。

修改时不要直接编辑构建生成的 app_v131.cpp、ocr_engine_v130.cpp、update_manager_fixed.cpp；这些文件会被脚本覆盖。先定位对应基础源文件或补丁脚本。
```

## 12. 交接验收清单

```text
[ ] main 可编译
[ ] 窗口尺寸与当前设计一致
[ ] 图片点击、拖拽、粘贴均可用
[ ] OCR 返回五条合法属性
[ ] 新图片导入时无手动状态闪烁
[ ] 停止识别可终止后台任务
[ ] 记录位置按空位前移
[ ] 清空全部后回到声骸 1
[ ] 五张卡总分正确
[ ] 平均分固定除以 5
[ ] PNG 可保存并可正常打开
[ ] GitHub 更新源可识别 v1.3.1
[ ] Gitee main 与标签已同步
[ ] Gitee Release 包含 zip 与 sha256
[ ] 旧版可完成在线更新
[ ] data 与 updates 未被覆盖
```
