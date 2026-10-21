# USCADAWidget

Unreal Engine 5.7 的类 WinCC SCADA 界面配置插件：基于 UMG + Slate 的封装绘图控件（线 / 折线 / 多边形 / 椭圆 / 圆 / 矩形 / 文字等工业图元），可在 UMG 设计器中像 WinCC 一样拖放、缩放、翻转图元，属性实时回写资产。

## 功能特性

### SCADA Canvas（画布）

- 继承 `UCanvasPanel`：设计器内直接鼠标拖动、锚点 gizmo、网格吸附等原生画布交互全部可用。
- 所有图元的容器，坐标一律使用**画布设计坐标**（左上角原点）。
- **分辨率自适应（`CanvasSize` + `ScaleMode`）**：按设计尺寸（默认 1920×1080）画图，运行时无论窗口/分辨率如何自动缩放整幅画面——`Fit` 等比完整显示（可能留边）、`Fill` 等比填满（超出裁切）、`Stretch` 非等比铺满（`0,0→CanvasSize` 恒等于可视区域四角，比例可变形）。位置、线宽、圆角、箭头全部等比跟随，图元内部坐标不变；`None`（默认）= 手动 `Zoom`（旧行为，已存资产无影响）。
- 手动 `Zoom`：以左上角为原点对子内容整体渲染缩放（ScaleMode ≠ None 时忽略）。
- 可选设计网格（`bShowGrid` / `GridSize` / `GridColor`），网格密度随生效缩放自适应。
- 自定义槽 `USCADACanvasSlot`：保留 UMG 设计器自由拖动/拉伸/锚点，槽变化实时回写图元真实资产实例（设计器拖动只写槽，不回写会保存后丢数据）；方向键微调固定 1px 步进。**SCADA 图元的槽锚点固定左上角**（设置其它锚点会被自动重置——图元以几何数据为真值，锚点相对坐标与该模型不兼容）；锚点仅对原生 UMG 控件（Text/Image 等）有意义，整幅画分辨率适配请用 ScaleMode。

### SCADA Line（直线）

对照 WinCC「线」对象：

- `LineStyle`：实线 / 虚线 / 点 / 点划线 / 双点划线（"画/空长度模式表"驱动，"点"为旋转实心方块）。
- `LineStartStyle` / `LineEndStyle`：默认 / 实心箭头，起终点独立，箭头尺寸自适应线宽。
- `LineCap`：无 / 圆形（端点实心圆盘，直径 = 线宽），与箭头互斥。
- **横平竖直不再死锁**：水平/竖直的线拖角 = 对应侧端点跟随拉斜（拖左上 = 左端点上移），中点把手 = 整体平移。

### SCADA Polyline（折线）

- `Points` 数组按顺序连接，复用 Line 的全部样式属性（线型 / 箭头 / 圆顶）。
- 拐角圆滑过渡：实线线体采用**笔画包络**绘制（中心线两侧偏移构造封闭轮廓，一次填色并柔化边缘），拐角自动以短圆弧过渡（默认半径 10px，随邻段长度自适应收缩，drawio 式肘部观感），任意角度连续无接缝，放大后同样平滑；虚线沿圆弧流动划线模式。
- **每个折点可在设计器中直接拖动手柄**编辑（编辑器模块 USCADAWidgetEditor）。
- **数组增点自动定位**：`+` 追加 = 终点增、元素右键 Insert 到 0 = 起点增，新点沿就近线段延长 100px（不再落在原点）；中间插入取两邻点中点。

### SCADA Polygon（多边形）

- `Points` 顶点数组，首尾自动闭合 + 填充，对照 WinCC「多边形」对象。
- `FillColor` / `FillPattern`（实心 / 透明）：耳切三角化填充，支持凹多边形。
- `BorderColor` / `Thickness` / `LineStyle`：闭合边框（**内画环带**，见下方"边线"通用说明）；虚线沿闭合环流动。
- **每个顶点可在设计器中直接拖动手柄**；数组增点自动落在被插入的边中点（不落原点）。

### SCADA Ellipse（椭圆）

- `Center` / `RadiusX` / `RadiusY`（画布设计坐标），槽矩形 = 椭圆包围盒。
- 边框颜色 / 宽度 / 线型 + 填充颜色 / 图案；拖过对边翻转天然无感（椭圆在自身包围盒内镜像 = 自身）。

### SCADA Circle（圆）

- `Center` / `Radius`，边框与填充属性同椭圆。
- WinCC 语义拖拽：拖边 = 对边中心固定，拖角 = 对角固定，翻边自动镜像。

### SCADA Regular Polygon（正多边形）

- `Center` / `Radius`（外接圆半径）+ `Sides`（边数 3~64）+ `StartAngle`（起始角，-90 = 顶点朝上）；边框与填充属性同圆。
- 槽包围盒中心恒等于几何中心：设 RenderTransform Pivot(0.5,0.5) 后**绕几何中心旋转零晃动**，配合 RenderTransformAngle 做泵叶轮/风扇等旋转动画。
- 拖拽行为与圆一致；无逐顶点手柄（顶点由参数推导，不可逐点编辑）。

### SCADA Rectangle（矩形 / 圆角矩形）

- `Position` / `Size` + `CornerRadiusX` / `CornerRadiusY`（圆角半径，0 = 直角）。
- 边框颜色 / 宽度 / 线型 + 填充颜色 / 图案。

### 边线（多边形 / 椭圆 / 圆 / 矩形通用）

- **内画语义**：几何数据（顶点 / 半径 / 尺寸）= 外轮廓 = 视觉边界 = 槽包围盒；边框整体画在图形内部，外轮廓不随边宽变化。
- **Thickness 允许 0** = 不画边框，且图形本体一点不小（不会"少一圈"）。
- 实线边框 = 环带四边形条带：外环 = 原始轮廓（尖角精确到几何顶点）、内环 = 内缩 T 的轮廓，逐边两个三角形零缝隙——尖角不外露填充；填充与边框分层绘制，边框稳定压在填充之上。**正圆与矩形（直角 / 等半径圆角）的实线样式走引擎解析圆角矩形（RoundedBox SDF）快速路径**：填充 + 内画边框单元素完成，数学精确曲线与解析抗锯齿；其余情形（虚线样式、不等半径椭圆角等）自动回退上述环带路径，语义与观感完全一致。
- **闪烁只切填充色**，边框保持本色（WinCC 语义：黑边白圆红闪 = 黑边红闪）。

### SCADA Text（文字）

- 直接继承 `UTextBlock`：文字内容 / 字体 / 字号 / 对齐 / 换行等全部沿用 UE 内置能力。
- 只增加闪烁属性（见下）。

### 通用

- 双向同步：图元几何数据是唯一真值，槽只是操作手柄；属性面板改坐标 → 槽跟随，设计器拖槽 → 坐标回写，编译/保存不丢数据。
- 设计器交互：拖动平移、拉伸缩放、**拖过对边镜像翻转**（WinCC 式）、方向键 1px 微调，松手自动取整并记入一次撤销事务。
- **顶点 / 端点直接拖动**：选中折线 / 多边形（每个顶点）或线段（两端点）后，画布上显示手柄直接拖动改坐标（撤销事务、双实例同步、取整）。由编辑器模块 `USCADAWidgetEditor` 的设计器扩展提供。
- **细节面板数值拖动降速**：坐标属性拖动恒为 1 单位/鼠标像素（默认速度与数值大小成正比，坐标上百时过快）。
- 闪烁：`bFlashEnabled` + `FlashColor`，全局统一 1 秒换相（墙钟驱动，所有图元天然同步），只切颜色；透明度 0 即隐藏。运行时同样生效。
- **相位翻转分组**：`bFlashInverted`（默认关）开启后与全局定时器取色相反——正常相位显示闪烁色、闪烁相位显示正常色。两组图元同一时刻换相、显示色互反（一组亮时另一组灭），用于状态互闪。线/折线控件默认属于翻转组，其余图元默认正常组。
- 像素对齐：坐标/线宽在数据层即取整，面板显示值 = 最终绘制值。
- **渲染平滑度（所有可见边缘均有抗锯齿）**：正圆与等半径圆角矩形走 RoundedBox SDF——解析曲线 + 解析抗锯齿，随画布 Zoom 任意缩放同样锐滑；Line / Polyline 实线为笔画包络填色，边缘柔化、拐角自动圆弧过渡；其余曲线图元（椭圆、不等半径圆角、多边形）的填充与边框内外缘均有同色柔边处理。与像素对齐不冲突：数据层整数化决定"画在哪"，渲染层抗锯齿决定"边缘多柔"。

## 环境要求

- Unreal Engine **5.7**
- 模块：
  - `USCADAWidget`（Runtime）：依赖 `Core / CoreUObject / Engine / UMG`（Public），`Slate / SlateCore / InputCore`（Private）
  - `USCADAWidgetEditor`（Editor）：依赖 `UMGEditor / UnrealEd` 等编辑器模块，向 UMG 设计器注册顶点拖动手柄扩展，不参与打包

## 安装

将本仓库克隆（或复制）到项目的 `Plugins/USCADAWidget` 目录，重新生成工程文件并编译即可：

```
<UE>/Engine/Build/BatchFiles/GenerateProjectFiles.bat -project="YourProject.uproject" -game -engine
```

## 使用

1. 在 UMG 设计器中搜索 `SCADA Canvas` 放入画布，作为所有图元的容器。
2. 向 Canvas 中添加 `SCADA Line` / `SCADA Polyline` / `SCADA Polygon` / `SCADA Ellipse` / `SCADA Circle` / `SCADA Regular Polygon` / `SCADA Rectangle` / `SCADA Text`。
3. 直接拖动/拉伸图元（支持拖过对边翻转、方向键微调），或在属性面板编辑坐标、线宽、颜色、线型、填充、闪烁等属性。
4. 选中折线/多边形/线段后，直接拖动画布上的顶点手柄微调形状；在细节面板数组里 `+`/Insert 增点会自动落在合理位置（延长线 / 边中点）。
5. 需要分辨率自适应时：画布 `CanvasSize` 设为设计分辨率（默认 1920×1080），`ScaleMode` 选 `Fit`（等比不变形）或 `Stretch`（设计区域精确铺满可视区域）——窗口化、非设计分辨率运行时整幅画面自动缩放；`GetEffectiveCanvasScale()` 可取当前生效缩放系数。

## 目录结构

```
Source/
├── USCADAWidget/             # Runtime 模块
│   ├── Public/
│   │   ├── SCADAWidgetTypes.h # 公共枚举/结构（全部 UENUM/USTRUCT(BlueprintType)；原名 SCADATypes.h，为避免与同工程其它插件同名头文件冲突而改名）
│   │   ├── Slate/            # 底层 SWidget（Canvas / Line / Polygon / Ellipse / Rectangle）+ 共享笔画几何工具 SCADAStrokeGeometry.h（笔画包络构造、耳切三角化，Line/Polygon 共用）
│   │   └── Widgets/          # UMG 层控件（Canvas / Line / Polyline / Polygon / Ellipse / Circle / Rectangle / Text / 槽）
│   └── Private/              # 对应实现
└── USCADAWidgetEditor/       # Editor 模块：设计器顶点拖动手柄扩展（FDesignerExtension）
```

## 后续规划

元件库（泵/阀/管道）、属性绑定与动画、更多图元专属设计器交互。

## 许可证

见 [LICENSE](LICENSE)。
