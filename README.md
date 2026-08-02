# WuwaEchoCalculator

鸣潮声骸截图识别与活动计分工具。当前版本为 **v1.3.1**，主程序采用 Win32、Direct2D、DirectWrite、WIC 与 ONNX Runtime，PP-OCRv5 在本机完成图片识别。

## 功能

- 支持点击选择、全窗口拖拽和 `Ctrl+V` 粘贴声骸截图。
- 自动识别五条辅音属性与档位，并支持手动修正。
- 记录五件声骸，动态显示单项分数、小计、总分和平均分。
- 支持编辑、删除、覆盖、清空和导出 PNG。
- 支持中止当前识别，以及 Gitee 优先、GitHub 回退的应用更新。
- 图片与记录只在本机处理和保存。

## 构建

正式 Windows 便携包由 GitHub Actions 构建。主程序直接编译 `native/` 下的权威 C++ 源码；Python 仅用于生成图标、OCR 测试图片，以及从模型配置提取识别字典，不参与业务源码生成。

主要工作流：

- `.github/workflows/build.yml`：构建与回归测试。
- `.github/workflows/native-validation.yml`：原生实现验证。
- `.github/workflows/publish-v131.yml`：v1.3.1 发布。

旧 WebView2 版本保存在分支 `backup/main-webview2-v1.2.2-20260802`。
