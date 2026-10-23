// Copyright Pr_UEDraw. All Rights Reserved.

#include "Widgets/SCADAPrimitiveWidgetBase.h"

#include "Slate/SSCADALine.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"

void USCADAPrimitiveWidgetBase::SetupSlotSyncCallbacks(SSCADALine* SlateWidget)
{
	// UWidget 没有原生 Tick，借 Slate 层的 Tick 轮询槽矩形变化。
	// 槽同步状态机纯服务 UMG 设计器（拖动回写/面板编辑/撤销识别/落点认领），
	// 运行时（PIE/游戏世界）不挂接（GetWorld()==nullptr 仍认编辑器：设计器预览实例可能拿不到世界）
	if (GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
	{
		TWeakObjectPtr<USCADAPrimitiveWidgetBase> WeakThis(this);
		SlateWidget->ExternalTickCallback = [WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->CheckSlotSync();
			}
		};
	}

	// archetype 实例（Slate 不一定被 Tick）也要能跑 CheckSlotSync，
	// 否则面板拖放的落点（静默写在 archetype 槽上）永远认领不到
	EnsureCoreTicker();
}

void USCADAPrimitiveWidgetBase::EnsureCoreTicker()
{
	if (CoreTickerHandle.IsValid())
	{
		return;
	}
	// 槽同步轮询纯服务 UMG 设计器：运行时（PIE/游戏世界）不注册核心 Ticker
	// （RebuildWidget/ForceSyncFromSlotRect 两处注册路径都经过这里，一处守卫全部生效）
	if (!GIsEditor || (GetWorld() && GetWorld()->IsGameWorld()))
	{
		return;
	}
	TWeakObjectPtr<USCADAPrimitiveWidgetBase> WeakThis(this);
	CoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CheckSlotSync();
			return true;
		}
		return false;
	}));
}

void USCADAPrimitiveWidgetBase::BeginDestroy()
{
	if (CoreTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
		CoreTickerHandle.Reset();
	}
	Super::BeginDestroy();
}

FVector2D USCADAPrimitiveWidgetBase::GetGeometryMin() const
{
	FVector2D Min, Max;
	GetGeometryBounds(Min, Max);
	return Min;
}

void USCADAPrimitiveWidgetBase::SyncSlotFromGeometry()
{
	if (bSyncingGeometry)
	{
		return;
	}
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		PushGeometryToSlate();
		return;
	}

	TGuardValue<bool> Guard(bSyncingGeometry, true);

	// 锚点强制左上角（双保险，含运行时 Rebuild/Synchronize 路径）：不变式"槽矩形 = 数据包围盒
	// （设计坐标）"只在左上角锚点下成立；非左上角锚点的槽位置是锚点相对偏移且随画布尺寸漂移。
	// 值相同则 Set 内部无实质写入，稳态零成本。
	const FAnchors CurAnchors = CanvasSlot->GetAnchors();
	if (CurAnchors.Minimum != FVector2D::ZeroVector || CurAnchors.Maximum != FVector2D::ZeroVector
		|| !CanvasSlot->GetAlignment().IsNearlyZero())
	{
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	}

	// 槽矩形 = 几何数据包围盒（min 位置 + 正尺寸，尺寸下限 1 防退化）
	FVector2D Min, Max;
	GetGeometryBounds(Min, Max);
	const FVector2D Size(FMath::Max(Max.X - Min.X, 1.0), FMath::Max(Max.Y - Min.Y, 1.0));

	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetPosition(Min);
	CanvasSlot->SetSize(Size);
	CachedSlotPos = Min;
	CachedSlotSize = Size;
	bSlotCacheValid = true;
	// 此刻数据包围盒 == 槽缓存：给子类一个记录时机（折线/多边形记录已验证点位版本）
	OnSlotAlignedToGeometry();
	// 几何数据为真值：任何正向同步都复位交互簿记（符号基线/收尾标记 + 子类交互状态）
	bPrevRawNegX = false;
	bPrevRawNegY = false;
	bPendingFinalize = false;
	ResetInteractionState();

	PushGeometryToSlate();
}

void USCADAPrimitiveWidgetBase::ForceSyncFromSlotRect()
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		EnsureCoreTicker();
		// 属性面板编辑/方向键微调等合法输入通道：授权一次映射（撤销/编译等后台重写不走这里）
		bSlotRemapAuthorized = true;
		CheckSlotSync();
	}
}

void USCADAPrimitiveWidgetBase::TranslateGeometryAndSync(const FVector2D& Delta)
{
	// 正向通道：SyncSlotFromGeometry 内部已完成写槽 + 刷新槽缓存（CachedSlotPos/Size、
	// bSlotCacheValid）+ PushGeometryToSlate + 交互簿记复位，此处不可重复调用。
	// 下一拍 CheckSlotSync 读到"槽 == 缓存 == 数据包围盒"，不会把这平移误判成
	// 设计器输入去做映射（裸写 slot->SetPosition 则会被当成外部拖动输入）。
	TranslateGeometryBy(Delta);
	SyncSlotFromGeometry();
}

void USCADAPrimitiveWidgetBase::CheckSlotSync()
{
	if (bSyncingGeometry)
	{
		return;
	}

	// 设计器拖动/拉伸槽的处理（显示与存储分离：几何数据是唯一真值，槽只是操作手柄）。
	// 槽永远保持正向规范矩形（min 位置 + 正尺寸），布局不受负尺寸影响，绘制不跳；
	// 映射在规范空间增量进行；翻转（WinCC 式拖过对边镜像）= 原始尺寸符号变化沿，
	// 过一次边在新矩形内镜像一次，可来回拖（规范化/翻转沿/映射核心在子类 MapSlotToGeometry）。
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	if (!CanvasSlot)
	{
		return;
	}

	// 锚点强制左上角（编辑器交互通道：用户在设计器改锚点/对齐，面板或 gizmo 均经槽属性通知
	// 到达本函数）：非左上角锚点的槽位置是锚点相对偏移、随画布尺寸漂移，绝不是合法映射输入
	// ——必须在读取/映射之前拦截，以数据为真值重写槽（SyncSlotFromGeometry 内含锚点复位），绝不映射。
	{
		const FAnchors CurAnchors = CanvasSlot->GetAnchors();
		if (CurAnchors.Minimum != FVector2D::ZeroVector || CurAnchors.Maximum != FVector2D::ZeroVector
			|| !CanvasSlot->GetAlignment().IsNearlyZero())
		{
			SyncSlotFromGeometry();
			return;
		}
	}

	const FVector2D RawPos = CanvasSlot->GetPosition();
	const FVector2D RawSize = CanvasSlot->GetSize();

	// 缓存未初始化（新实例首次 Tick）：只采纳当前槽矩形为基准，不做映射，
	// 否则默认 1x1 缓存会把健康槽矩形误判成外部缩放输入，把几何数据放大到爆炸。
	if (!bSlotCacheValid)
	{
		bSlotCacheValid = true;
		// 初始放置认领：拖放时设计器可能在首次 Tick 之前就把槽写到落点（SynchronizeProperties
		// 已跑过，钩子 1 错过），若只采纳基准会把落点收下、数据留在默认位置（松手跳回原点），
		// 之后 Raw!=Cache 时下方钩子 2 再用陈旧数据原点平移 = 位置突变。
		// 所以认领标记仍在时先平移数据到槽位置并正向对齐，再以对齐后的槽矩形为基准。
		// 守卫限定设计器：UMG 设计器实例的 GetWorld() 返回编辑器世界（非空！），
		// 用 !IsGameWorld() 放行编辑器/预览世界，只屏蔽 PIE/打包游戏。
		if (bPendingInitialPlacement && GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
		{
			bPendingInitialPlacement = false;
			const FVector2D RawMin = RawPos + FVector2D(FMath::Min((float)RawSize.X, 0.0f), FMath::Min((float)RawSize.Y, 0.0f));
			TranslateGeometryBy(RawMin - GetGeometryMin());
			SyncSlotFromGeometry();
			CachedSlotPos = CanvasSlot->GetPosition();
			CachedSlotSize = CanvasSlot->GetSize();
		}
		else
		{
			CachedSlotPos = RawPos;
			CachedSlotSize = RawSize;
		}
		// 统一取 CachedSlotSize：else 分支它刚被赋为 RawSize（两者恒等）；
		// 认领分支它已被 SyncSlotFromGeometry 正向对齐（恒正 → false），与原 5 份逐字一致。
		// （Circle 原本取 RawSize，但其符号基线只写不读——镜像 = 自身、无翻转沿消费，改写后无可观察差异）
		bPrevRawNegX = CachedSlotSize.X < 0.0;
		bPrevRawNegY = CachedSlotSize.Y < 0.0;
		OnSlotCacheAdopted();
		return;
	}

	// 鼠标按键查询惰性化：稳态空转帧（槽矩形无变化，占绝大多数帧）不做这次哈希查找，
	// 仅在收尾判定/授权门闸实际需要时才取值。
	const auto IsLeftMouseDown = []() { return FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton); };

	if (RawPos.Equals(CachedSlotPos) && RawSize.Equals(CachedSlotSize))
	{
		// 无新输入：作废未消耗的授权，避免遗留授权让下一次后台重写被误判成输入
		bSlotRemapAuthorized = false;

		// 设计器无新输入。鼠标左键仍按下 = 拖动中停顿，不收尾（保留符号沿状态）；
		// 已松开 = 交互结束，收尾一次：取整、标脏、符号基线复位。
		if (bPendingFinalize && !IsLeftMouseDown())
		{
			bPendingFinalize = false;
			bInteractionModified = false;
			bPrevRawNegX = false;
			bPrevRawNegY = false;
			ResetInteractionState();
			SnapPropertiesToPixel();
			SyncSlotFromGeometry();
#if WITH_EDITOR
			if (GIsEditor && !HasAnyFlags(RF_Transient))
			{
				MarkPackageDirty();
			}
#endif
		}
		else if (!bPendingFinalize && GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
		{
			// 失配自愈：撤销/后台恢复只还原了数据或槽的一边时（槽=缓存=拖拽后、
			// 数据=拖拽前），数据包围盒与缓存不一致 → 以数据为真值对齐槽，
			// 防止错位的槽被后续交互当成基准。
			// ShouldVerifyGeometryBounds：折线/多边形的版本号短路（点位未变则包围盒必与上次
			// 确认时一致，跳过 O(N) 遍历）；其余图元恒 true。
			if (ShouldVerifyGeometryBounds())
			{
				FVector2D BoundsMin, BoundsMax;
				GetGeometryBounds(BoundsMin, BoundsMax);
				const FVector2D BoundsSize(FMath::Max(BoundsMax.X - BoundsMin.X, 1.0), FMath::Max(BoundsMax.Y - BoundsMin.Y, 1.0));
				if (!BoundsMin.Equals(CachedSlotPos) || !BoundsSize.Equals(CachedSlotSize))
				{
					SyncSlotFromGeometry();
				}
				else
				{
					// 一致：给子类一个记录时机（折线/多边形记录已验证版本，
					// 失配路径由 SyncSlotFromGeometry 内部经 OnSlotAlignedToGeometry 记录）
					OnGeometryBoundsVerified();
				}
			}
		}
		return;
	}

	// 初始放置认领（设计器在首次同步之后才把槽写到落点，未经属性通知）：
	// 平移数据到槽位置并正向对齐，不做映射。守卫限定设计器（编辑器/预览世界放行，
	// PIE/打包游戏屏蔽），防止运行时外部代码改槽位置时误触发平移。
	if (bPendingInitialPlacement && GIsEditor && (!GetWorld() || !GetWorld()->IsGameWorld()))
	{
		bPendingInitialPlacement = false;
		// 消耗 ForceSyncFromSlotRect 置的一次性授权：若被认领先消耗不掉，
		// 下一次后台重写会被误判成授权输入。
		bSlotRemapAuthorized = false;
		const FVector2D RawMin = RawPos + FVector2D(FMath::Min((float)RawSize.X, 0.0f), FMath::Min((float)RawSize.Y, 0.0f));
		TranslateGeometryBy(RawMin - GetGeometryMin());
		SyncSlotFromGeometry();
		return;
	}

	// 撤销/还原识别（先于授权门闸）：槽与当前数据包围盒完全一致、但与缓存不同 =
	// 外部整体还原（撤销经事务序列化同时恢复槽和数据，二者天然一致）→ 对齐缓存即可，
	// 绝不可映射——映射会按缓存比例缩放这份一致状态导致数据爆炸（撤销走样根因）。
	// 真实拖动/面板输入时槽与数据必然不一致（数据滞后于槽）不会误判；误判也无害：
	// 槽==包围盒时映射本就是恒等变换。撤销经属性通知到达时授权位可能为真，故必须在门闸前。
	{
		FVector2D DataMin, DataMax;
		GetGeometryBounds(DataMin, DataMax);
		const FVector2D DataBoundsSize(FMath::Max(DataMax.X - DataMin.X, 1.0), FMath::Max(DataMax.Y - DataMin.Y, 1.0));
		if (RawPos.Equals(DataMin) && RawSize.Equals(DataBoundsSize))
		{
			bPendingFinalize = false;
			bInteractionModified = false;
			bPrevRawNegX = false;
			bPrevRawNegY = false;
			ResetInteractionState();
			SyncSlotFromGeometry();
			return;
		}
	}

	// 撤销/后台重写识别：鼠标未按下且未经属性面板授权（bSlotRemapAuthorized）的槽输入
	// 不是用户输入——撤销经事务序列化恢复槽/数据（不走属性通知通道），若当作设计器新输入
	// 映射会把刚恢复的数据再改写一遍（撤销当下走样）。一律拒绝映射、以数据为真值对齐槽。
	// 合法通道：设计器拖动（鼠标按下）、面板编辑/方向键微调/拖放落点（经
	// NotifyContentSlotGeometryChanged → ForceSyncFromSlotRect 置一次性授权）。
	if (!IsLeftMouseDown() && !bSlotRemapAuthorized)
	{
		bPendingFinalize = false;
		bInteractionModified = false;
		bPrevRawNegX = false;
		bPrevRawNegY = false;
		ResetInteractionState();
		SyncSlotFromGeometry();
#if WITH_EDITOR
		if (GIsEditor && !HasAnyFlags(RF_Transient))
		{
			MarkPackageDirty();
		}
#endif
		return;
	}
	bSlotRemapAuthorized = false;

	// 设计器写入了新矩形（不变式：读到与缓存不同的值即外部输入）
#if WITH_EDITOR
	if (!bInteractionModified)
	{
		// 每次交互只进一次撤销事务
		bInteractionModified = true;
		if (GIsEditor && !HasAnyFlags(RF_Transient))
		{
			Modify();
		}
	}
#endif
	// 交互首帧取样（bPendingFinalize 置位前判定）：此刻缓存仍是鼠标按下时的槽值
	// （= 设计器 StartingOffsets 快照），子类映射核心的交互起点簿记据此取样。
	const bool bInteractionStart = !bPendingFinalize;
	bPendingFinalize = true;
	MapSlotToGeometry(CanvasSlot, RawPos, RawSize, bInteractionStart);
}
