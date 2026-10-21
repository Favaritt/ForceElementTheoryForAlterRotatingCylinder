# ForceElementTheoryForAlterRotatingCylinder

OpenFOAM 自定义 function object，用于圆柱流动中的力元贡献积分与辅助场输出。

An OpenFOAM function object for force-element contributions in cylinder flows.

## 本仓库的整理范围

- 未收录 `lnInclude/`、平台编译目录、目标文件等可再生成文件。
- 原始文件校验值见 [docs/original-files.sha256](docs/original-files.sha256)。

实际配置、输出应以实现文件及本 README 为准。当前实现输出六个贡献量。

## 目录

```text
.
├── ForceElementTheoryForAlterRotatingCylinder/
│   ├── ForceElementTheoryForAlterRotatingCylinder.C
│   └── ForceElementTheoryForAlterRotatingCylinder.H
├── Make/
│   ├── files
│   └── options
├── examples/functions.dict
├── docs/
│   ├── original-files.sha256
│   └── VALIDATION.md
├── Allwmake
├── Allwclean
└── LICENSE
```

## 编译

需要 Linux/WSL、匹配的 OpenFOAM 开发环境与 C++ 编译器。先加载本机 OpenFOAM 的 `etc/bashrc`，然后在仓库根目录执行：

```sh
./Allwmake
# 等效命令：wmake libso
```

库文件按原始 `Make/files` 输出到：

```text
$FOAM_USER_LIBBIN/libfieldFunctionObjects_USER_FEMAlterRotatingCylinder.so
```

清理本项目生成的编译文件：

```sh
./Allwclean
```

原始构建配置包含较广的 OpenFOAM 库依赖，保留原样。版本兼容性与本次检查结果见 [验证记录](docs/VALIDATION.md)。本仓库未提供完整算例、网格或基准结果。

## 在算例中启用

将 [examples/functions.dict](examples/functions.dict) 中的 `cylinderForceX` 块放入算例 `system/controlDict` 的 `functions { ... }` 内，或在该块内使用：

```foam
functions
{
    #include "functions.dict"
}
```

使用后一种写法时，将示例文件复制到算例的 `system/functions.dict`。示例数值只是配置格式示意，必须替换为实际算例的几何、密度和壁面名称，然后正常启动求解器。

| 配置项 | 含义与实现约束 |
| --- | --- |
| `type` | `ForceElementTheoryForAlterRotatingCylinder` |
| `libs` | `("libfieldFunctionObjects_USER_FEMAlterRotatingCylinder.so")` |
| `field` | 速度场名，默认 `U`，须具有速度量纲 |
| `wallID` | 必填、非空的实际 wall patch 名称列表，不允许重复 |
| `radius` | 圆柱半径，长度量纲，默认 1，必须大于 0 |
| `rho` | 常密度，密度量纲，默认 1，必须大于 0 |
| `potentialSpeed` | 辅助势参考速度，速度量纲，默认 1；请使用正值，源码中含对此值的除法 |
| `cylinderCentre` | 圆柱中心坐标，默认 `(0 0 0)`；圆柱轴平行 z |
| `forceDirection` | xy 平面内非零受力方向，程序归一化；默认 `(1 0 0)`，`(0 1 0)` 用于 y 方向 |
| `executeControl` / `executeInterval` | 必须保持 `timeStep` / `1` |
| `writeControl` / `writeInterval` | 标准 function object 的时间序列写出控制 |
| `writePrecision` | 默认 15，允许 1 到 17 |
| `writeFields` | 是否写辅助场，默认 `false` |
| `fieldPrefix` | 辅助场前缀，默认对象名加 `_` |
| `fieldWriteControl` | `none`、`timeStep`、`writeTime` 或 `runTime`；默认 `writeTime` |
| `fieldWriteInterval` | 正数，默认 1；`timeStep` / `writeTime` 时须为整数 |

速度、壁面速度与边界条件由已有算例提供。本对象不设置圆柱旋转运动。黏度优先读取已注册的 `turbulenceModel.nuEff()`；无该对象时，读取已注册的 `transportProperties` 字典中的常运动黏度 `nu`。

## 输出

默认单区域算例的时间序列位置为：

```text
postProcessing/<对象名>/<本次启动时刻>/force_FEM.dat
```

多区域时在对象名与启动时刻之间增加区域名。文件扩展名为 `.dat`，内容是带表头的逗号分隔数据；续算使用本次启动时间目录，同一文件按追加方式写入。

| 列 | 实现含义 |
| --- | --- |
| `Time` | 当前仿真时间 |
| `wall` | 当前壁面 patch 名称 |
| `SumFlowStructureContribution` | 全流体域体积流动结构贡献 |
| `SumNegativeFlowStructure` | 全流体域负贡献之和 |
| `SumPositiveFlowStructure` | 全流体域正贡献之和 |
| `SumWallVorticityContribution` | 当前壁面的涡量贡献 |
| `SumWallShearStressContribution` | 当前壁面的剪切贡献 |
| `SumWallVelocityContribution` | 当前壁面的速度贡献 |

同一对象指定多个壁面时，每个壁面输出一行；前三个流动结构量来自同一个全域积分，会在各行重复，不能将这些行的全域量再次相加。输出为当前网格体积/面积上的有量纲积分，代码未按展向长度或动压作无量纲化。文件没有额外的总力或力系数列。

启用 `writeFields` 后，可输出带 `fieldPrefix` 的 `VorticityFET`、`LambVectorFET`、`Potential`、`GradPotential`、`FlowStructureContribution`、`WallVorticityContribution`、`WallShearStressContribution` 和 `WallVelocityContribution`。壁面贡献场只在目标壁面赋值；解析势及其梯度的边界赋值仅针对 wall patch。不要将其他边界的默认值当作解析结果。

## 验证与使用边界

解析辅助势假定圆柱轴平行 z、方向位于 xy 平面；体积/面心落在圆柱势奇点附近会触发错误。配置中的正参考速度应由使用者保证。

## 许可证

保留源文件已有的 OpenFOAM Foundation / OpenCFD 版权声明与 GNU GPL 第 3 版或后续版本条款（`GPL-3.0-or-later`）。许可证全文见 [LICENSE](LICENSE)。
