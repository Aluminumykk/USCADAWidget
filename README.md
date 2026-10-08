# USCADAWidget

Unreal Engine 5.7 的类 WinCC SCADA 界面配置插件：基于 UMG + Slate 的封装绘图控件（画线 / 工业图元），可在 UMG 设计器中像 WinCC 一样拖放、缩放、翻转线条图元，属性实时回写资产。

## 功能特性

### SCADA Canvas（画布）

- 所有图元的容器，坐标一律使用**画布设计坐标**（左上角原点）。
- 跨分辨率适配只调画布 `Zoom`：以左上角为原点对子内容整体渲染缩放，图元内部坐标与线宽、圆角、箭头全部等比跟随。
- 自定义槽 `USCADACanvasSlot`：保留 UMG 设计器自由拖动/拉伸/锚点，槽变化实时回写图元真实资产实例（设计器拖动只写槽，不回写会保存后丢数据）。

### SCADA Line（直线）

对照 WinCC「线」对象：

- `LineStyle`：实线 / 虚线 / 点 / 点划线 / 双点划线（"画/空长度模式表"驱动，"点"为旋转实心方块）。
- `LineStartStyle` / `LineEndStyle`：默认 / 实心箭头，起终点独立，箭头尺寸自适应线宽（`MakeCustomVerts` 单三角实心填充）。
- `LineCap`：无 / 圆形（端点实心圆盘，直径 = 线宽，`FSlateRoundedBoxBrush` 全圆一次 `MakeBox`），与箭头互斥。

### SCADA Polyline（折线）

- `Points` 数组按顺序连接，复用 Line 的全部样式属性。
- 拐点圆连接（Round Join）：实线 = 原始折线 + 每个中间顶点直径 = 线宽的实心连接盘，任意角度连续无接缝；虚线沿圆弧流动划线模式。
- 槽整体变换：拖动平移、拉伸等比缩放、**拖过对边镜像翻转**（WinCC 式），松手自动取整。

### 通用

- 双向同步：点位是唯一真值，槽只是操作手柄；属性面板改坐标 → 槽跟随，设计器拖槽 → 坐标回写，编译/保存不丢数据。
- 闪烁：`bFlashEnabled` + `FlashColor`，全局统一 1 秒换相（墙钟驱动，所有图元天然同步），只切颜色；透明度 0 即隐藏。
- 像素对齐：坐标/线宽在数据层即取整，面板显示值 = 最终绘制值。

## 环境要求

- Unreal Engine **5.7**
- 模块类型：Runtime，依赖 `UMG / Slate / SlateCore`

## 安装

将本仓库克隆（或复制）到项目的 `Plugins/USCADAWidget` 目录，重新生成工程文件并编译即可：

```
<UE>/Engine/Build/BatchFiles/GenerateProjectFiles.bat -project="YourProject.uproject" -game -engine
```

## 使用

1. 在 UMG 设计器中搜索 `SCADA Canvas` 放入画布，作为所有图元的容器。
2. 向 Canvas 中添加 `SCADA Line` / `SCADA Polyline`。
3. 直接拖动/拉伸图元（支持拖过对边翻转），或在属性面板编辑点位、线宽、颜色、线型、箭头、圆顶、闪烁等属性。

## 目录结构

```
Source/USCADAWidget/
├── Public/
│   ├── SCADATypes.h          # 公共枚举/结构（全部 UENUM/USTRUCT(BlueprintType)）
│   ├── Slate/                # 底层 SWidget（SSCADACanvas / SSCADALine）
│   └── Widgets/              # UMG 层控件（SCADA Canvas / Line / Polyline / 槽）
└── Private/                  # 对应实现
```

## 后续规划

元件库（泵/阀/管道）、属性绑定与动画、`USCADAWidgetEditor` 编辑器模块。

## 许可证

见 [LICENSE](LICENSE)。
