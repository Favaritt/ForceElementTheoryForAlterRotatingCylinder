/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | www.openfoam.com
     \\/     M anipulation  |
-------------------------------------------------------------------------------
    Copyright (C) 2014-2016 OpenFOAM Foundation
    Copyright (C) 2016-2020 OpenCFD Ltd.
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Class
    Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder

Group
    grpFieldFunctionObjects

Description
    Computes the ForceElementTheoryForAlterRotatingCylinder, the curl of the velocity.

    \f[
        \vec \omega = \nabla \times \vec U
    \f]

    where
    \vartable
        \vec \omega   | ForceElementTheoryForAlterRotatingCylinder vector    [1/s]
        \vec U        | Velocity vector     [m/s]
    \endvartable

    Operands:
    \table
      Operand        | Type           | Location
      input          | volVectorField | $FOAM_CASE/\<time\>/\<inpField\>
      output file    | -              | -
      output field   | volVectorField | $FOAM_CASE/\<time\>/\<outField\>
    \endtable

Usage
    Minimal example by using \c system/controlDict.functions:
    \verbatim
    ForceElementTheoryForAlterRotatingCylinder2DCircle1
    {
        // Mandatory entries (unmodifiable)
        type        ForceElementTheoryForAlterRotatingCylinder;
        libs        (fieldFunctionObjects);

        // Optional (inherited) entries
        ...
    }
    \endverbatim

    where the entries mean:
    \table
      Property     | Description                        | Type | Req'd | Dflt
      type         | Type name: ForceElementTheoryForAlterRotatingCylinder               | word |  yes  | -
      libs         | Library name: fieldFunctionObjects | word |  yes  | -
    \endtable

    The inherited entries are elaborated in:
     - \link functionObject.H \endlink
     - \link fieldExpression.H \endlink

    Minimal example by using the \c postProcess utility:
    \verbatim
        postProcess -func ForceElementTheoryForAlterRotatingCylinder
    \endverbatim

See also
    - Foam::functionObject
    - Foam::functionObjects::fvMeshFunctionObject
    - Foam::functionObjects::fieldExpression
    - ExtendedCodeGuide::functionObjects::field::ForceElementTheoryForAlterRotatingCylinder

SourceFiles
    ForceElementTheoryForAlterRotatingCylinder.C

\*---------------------------------------------------------------------------*/

#ifndef functionObjects_ForceElementTheoryForAlterRotatingCylinder_H
#define functionObjects_ForceElementTheoryForAlterRotatingCylinder_H

#include "fvMeshFunctionObject.H" // 引入标准有限体积网格功能对象基类。
#include "timeControl.H" // 引入 OpenFOAM 时间控制器，供独立流场输出时钟使用。
#include "FixedList.H" // 引入固定长度数组类型，用于保存每行九个贡献值。
#include "IOobject.H" // 引入场文件名称、位置和读写选项的描述类型。

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace functionObjects
{

/*---------------------------------------------------------------------------*\
                          Class ForceElementTheoryForAlterRotatingCylinder Declaration
\*---------------------------------------------------------------------------*/
// 圆柱轴沿 z；使用正 x 方向力元理论的辅助势。
// 标准 writeControl/writeInterval 控制 CSV 时间序列输出。
// fieldWriteControl/fieldWriteInterval 独立控制辅助流场输出。
class ForceElementTheoryForAlterRotatingCylinder
:
    public fvMeshFunctionObject // 公有继承网格功能对象基类，从而访问求解器内存中的网格和场。
{ // 开始类定义；此处到 public 之前的成员默认是私有成员。
    word fieldName_; // 保存输入速度场的名称，默认使用 U。
    vector forceDirection_; // 保存归一化的受力方向；可选阻力、升力或任意非零 xy 平面方向。
    wordList wallID_; // 保存要积分的圆柱壁面 patch 名称列表。
    dimensionedScalar radius_; // 保存圆柱半径 R，参与势函数和曲率应力计算。
    dimensionedScalar potentialSpeed_; // 保存正的参考平移速度，量纲为 L/T，用于构造有量纲速度势。
    dimensionedScalar rho_; // 保存常密度 rho，用于把运动学贡献转换为力。
    vector cylinderCentre_; // 保存圆柱中心坐标，圆柱轴平行 z 方向。
    bool writeFields_; // 控制是否写出辅助流场；不影响时间序列输出。
    word fieldPrefix_; // 保存输出场名前缀；默认使用对象名加下划线，区分不同受力方向。
    label precision_; // 保存时间序列数值输出的小数位数。

    Foam::timeControl fieldWriteControl_; // 独立控制流场输出时刻的 OpenFOAM 时钟。
    const word outputStartTime_; // 固定保存本次运行启动时刻的目录名，构造后不再改变。
    label lastFieldCheckIndex_; // 记录上次检查流场时钟的时间步索引，防止同一步重复检查。
    label lastCalculationIndex_; // 记录上次完成积分计算的时间步索引，用于复用结果。
    label lastSeriesIndex_; // 记录上次追加时间序列的时间步索引，防止重复写行。

    // 以下4个数按原程序顺序排列，输出时位于 Time 和 wall 两列之后。
    typedef FixedList<scalar, 6> resultRow; // 把固定含4个 scalar 的数组命名为 resultRow，对应4列贡献。
    List<resultRow> results_; // 按目标壁面存储结果行，每个壁面保存一个 resultRow。

    static dictionary fieldControls(const dictionary& dict); // 声明静态辅助函数：复制并补全流场输出控制配置。
    IOobject fieldIO(const word& fieldName) const; // 声明只读辅助函数：创建一个辅助场所需的 IOobject。
    bool calc(const bool outputFields); // 声明计算函数：参数决定是否同时写出辅助场。
    bool writeSeries(); // 声明时间序列追加函数，只由主处理器实际写文件。

public:

    //- Runtime type information
    TypeName("ForceElementTheoryForAlterRotatingCylinder");


    // Constructors

        //- Construct from Time and dictionary
        ForceElementTheoryForAlterRotatingCylinder
        (
            const word& name,
            const Time& runTime,
            const dictionary& dict
        );

        //- No copy construct
        ForceElementTheoryForAlterRotatingCylinder(const ForceElementTheoryForAlterRotatingCylinder&) = delete;

        //- No copy assignment
        void operator=(const ForceElementTheoryForAlterRotatingCylinder&) = delete;


    //- Destructor
    virtual ~ForceElementTheoryForAlterRotatingCylinder() = default;
    virtual bool read(const dictionary& dict); // 声明配置读取接口，支持功能对象框架重新读取字典。
    virtual bool execute(); // 声明每步执行接口，用于检查独立流场输出时钟。
    virtual bool write(); // 声明写出接口，按标准 writeControl 追加时间序列。
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace functionObjects
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
