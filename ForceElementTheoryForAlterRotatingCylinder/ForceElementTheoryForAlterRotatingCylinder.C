/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | www.openfoam.com
     \\/     M anipulation  |
-------------------------------------------------------------------------------
    Copyright (C) 2014-2016 OpenFOAM Foundation
    Copyright (C) 2020 OpenCFD Ltd.
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

\*---------------------------------------------------------------------------*/

#include "ForceElementTheoryForAlterRotatingCylinder.H" // 引入本功能对象的类声明，使实现与头文件对应。
#include "volFields.H" // 引入体标量场、体矢量场等有限体积场类型。
#include "surfaceFields.H" // 引入面场类型，供网格面几何数据等接口使用。
#include "fvcGrad.H" // 引入显式空间梯度算子 fvc::grad。
#include "fvcCurl.H" // 引入显式旋度算子 fvc::curl，用于计算涡量。
#include "IOdictionary.H" // 引入传输物性字典接口，用于 icoFoam 层流回退。
#include "turbulenceModel.H" // 引入通用湍流模型接口，从中读取有效运动黏度 nuEff。
#include "wallPolyPatch.H" // 引入壁面 patch 类型，检查 wallID 是否确实指向壁面。
#include "addToRunTimeSelectionTable.H" // 引入运行时选择表注册宏，使 controlDict 可以按 type 创建本对象。
#include "PstreamReduceOps.H" // 引入并行归约操作，把各处理器的局部积分求和。
#include "OSspecific.H" // 引入文件夹创建等操作系统接口。
#include <fstream> // 引入标准文件输出流 std::ofstream。
#include <iomanip> // 引入输出格式和精度控制，例如 std::setprecision。
// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace functionObjects
{
    defineTypeNameAndDebug(ForceElementTheoryForAlterRotatingCylinder, 0);
    addToRunTimeSelectionTable(functionObject, ForceElementTheoryForAlterRotatingCylinder, dictionary);
}
}

Foam::dictionary // 声明下面这个函数返回一份 Foam::dictionary 字典。
Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::fieldControls // 定义 fieldControls：补全独立流场输出时钟的配置。
( // 开始上一行声明或表达式的多行括号内容。
    const dictionary& dict // 以只读引用接收用户配置，避免修改传入字典。
) // 结束当前参数表、条件表达式或分组表达式。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    dictionary controls(dict); // 复制用户字典，后续只在这个副本里补默认设置。
    if (!controls.found("fieldWriteControl")) // 检查用户是否没有指定流场输出的控制方式。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        controls.add("fieldWriteControl", word("writeTime")); // 缺省时随求解器的主写出时刻输出辅助流场。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    if (!controls.found("fieldWriteInterval")) // 检查用户是否没有指定流场输出间隔。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        controls.add("fieldWriteInterval", 1); // 将流场输出间隔的默认值补为 1。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    return controls; // 返回补全后的字典副本。
} // 结束当前命名空间、函数体或条件/循环代码块。

Foam::IOobject // 声明下面这个函数返回一个 Foam::IOobject。
Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::fieldIO // 定义 fieldIO：统一生成各辅助场的名称和读写设置。
( // 开始上一行声明或表达式的多行括号内容。
    const word& fieldName // 接收待创建辅助场的基础名称。
) const // 结束参数表；const 表示本函数不修改对象成员。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    // 辅助场只通过显式 write 写出；局部场不注册到网格对象表。
    return IOobject // 构造并返回辅助场的输入输出描述对象。
    ( // 开始上一行声明或表达式的多行括号内容。
        fieldPrefix_ + fieldName, // 把可选前缀加到基础场名前面，得到实际场名。
        mesh_.time().timeName(), // 把场的输出位置设为当前仿真时间目录。
        mesh_, // 关联当前网格，确定区域和文件所在的数据库。
        IOobject::NO_READ, // 禁止从已有时间目录读取该辅助场，始终重新计算。
        IOobject::NO_WRITE, // 禁止随网格自动写出；仅在指定频率触发时显式写出。
        false // 不把这些局部辅助场注册到网格对象表，避免名称占用。
    ); // 结束当前函数调用或对象构造语句。
} // 结束当前命名空间、函数体或条件/循环代码块。

Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder:: // 指定构造函数所属的命名空间和类。
ForceElementTheoryForAlterRotatingCylinder // 定义构造函数：创建一个运行时后处理对象。
( // 开始上一行声明或表达式的多行括号内容。
    const word& name, // 接收功能对象名称，也是 postProcessing 下的目录名。
    const Time& runTime, // 接收当前仿真的时间控制对象。
    const dictionary& dict // 接收本功能对象在 controlDict 中的配置字典。
) // 结束当前参数表、条件表达式或分组表达式。
: // 开始基类和成员变量的初始化列表。
    fvMeshFunctionObject(name, runTime, dict), // 初始化标准网格功能对象基类，绑定名称、时间和网格。
    fieldName_("U"), // 将待处理速度场的默认名称设为 U。
    forceDirection_(1, 0, 0), // 将默认受力方向设为正 x；用户可在字典中改为升力或其它平面方向。
    radius_("radius", dimLength, scalar(1), dict), // 先把圆柱半径初始化为 1，随后由 read 读取实际配置。
    potentialSpeed_("potentialSpeed", dimVelocity, scalar(1), dict), // controlDict 中的条目名称,在代码中规定量纲为速度 L/T,未填写该条目时，默认数值为 1,从当前功能对象的配置字典读取
    rho_("rho", dimDensity, scalar(1), dict), // 先把常密度初始化为 1，随后由 read 读取实际配置。
    cylinderCentre_(vector::zero), // 将圆柱中心的默认坐标设为原点。
    writeFields_(false), // 默认关闭辅助流场输出，保留时间序列处理。
    fieldPrefix_(word::null), // 默认不在输出场名前添加前缀。
    precision_(15), // 将时间序列的默认输出精度设为 15 位小数。
    fieldWriteControl_(runTime, fieldControls(dict), "fieldWrite"), // 创建独立流场时钟，读取 fieldWriteControl 和 fieldWriteInterval。
    outputStartTime_(Time::timeName(runTime.startTime().value())), // 保存本次运行的启动时刻名称，续算时用它新建结果子目录。
    lastFieldCheckIndex_(-1), // 用 -1 表示尚未检查过任何时间步的流场输出时钟。
    lastCalculationIndex_(-1), // 用 -1 表示当前尚无已经计算并缓存的积分结果。
    lastSeriesIndex_(-1) // 用 -1 表示尚未追加过任何时间步的时间序列。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    read(dict); // 读取并检查实际配置，覆盖上面初始化的默认值。
} // 结束当前命名空间、函数体或条件/循环代码块。

bool Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::read // 定义 read：读取或重新读取配置，返回是否成功。
( // 开始上一行声明或表达式的多行括号内容。
    const dictionary& dict // 接收最新配置字典的只读引用。
) // 结束当前参数表、条件表达式或分组表达式。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    if (!fvMeshFunctionObject::read(dict)) // 先让标准基类读取通用功能对象配置，若失败则进入下面分支。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        return false; // 将基类读取失败的状态返回给调用方。
    } // 结束当前命名空间、函数体或条件/循环代码块。

    fieldName_ = dict.lookupOrDefault<word>("field", "U"); // 读取速度场名；未设置 field 时使用 U。
    forceDirection_ = // 准备读取用户选择的阻力、升力或其它平面受力方向。
        dict.lookupOrDefault<vector>("forceDirection", vector(1, 0, 0)); // 读取 forceDirection，缺省采用正 x 方向。
    wallID_ = dict.get<wordList>("wallID"); // 读取必须设置的壁面名称列表 wallID。
    radius_ = dimensionedScalar("radius", dimLength, scalar(1), dict); // 读取必须设置的圆柱半径 radius。
    potentialSpeed_ = dimensionedScalar("potentialSpeed", dimVelocity, scalar(1), dict);
    rho_ = dimensionedScalar("rho", dimDensity, scalar(1), dict); // 读取必须设置的常密度 rho。
    cylinderCentre_ = // 准备读取圆柱中心坐标。
        dict.lookupOrDefault<vector>("cylinderCentre", vector::zero); // 读取 cylinderCentre；缺省取原点。
    writeFields_ = dict.lookupOrDefault<bool>("writeFields", false); // 读取是否写辅助场的开关，缺省为 false。
    fieldPrefix_ = dict.lookupOrDefault<word>("fieldPrefix", word(name() + "_")); // 缺省用对象名加下划线作为场名前缀，使阻力和升力对象的场名相互独立。
    precision_ = dict.lookupOrDefault<label>("writePrecision", 15); // 读取时间序列精度，缺省为 15。

    if // 开始条件判断，条件在后面的括号中展开。
    ( // 开始上一行声明或表达式的多行括号内容。
        radius_.value() <= 0 || rho_.value() <= 0 || wallID_.empty() // 检查半径、密度是否非正，以及壁面列表是否为空。
     || precision_ < 1 || precision_ > 17 // 继续检查输出精度是否超出允许的 1 到 17。
    ) // 结束当前参数表、条件表达式或分组表达式。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalIOErrorInFunction(dict) // 创建包含字典位置的致命输入错误信息。
            << "Require radius > 0, rho > 0, non-empty wallID, and " // 写入半径、密度及壁面列表的有效性要求。
            << "1 <= writePrecision <= 17." << exit(FatalIOError); // 补充精度范围要求，然后以输入错误终止程序。
    } // 结束当前命名空间、函数体或条件/循环代码块。

    const scalar directionMag = mag(forceDirection_); // 计算用户指定受力方向的模长。
    if (directionMag < SMALL || mag(forceDirection_.z()) > SMALL*directionMag) // 要求方向非零，且位于垂直于圆柱轴的 xy 平面内。
    { // 开始处理不支持的受力方向。
        FatalIOErrorInFunction(dict) // 创建带字典位置的致命配置错误。
            << "forceDirection must be non-zero and lie in the xy plane." // 说明该解析圆柱辅助势只支持 xy 平面内的非零方向。
            << exit(FatalIOError); // 终止运行，避免将二维辅助势用于轴向力。
    } // 结束方向有效性检查。
    forceDirection_.z() = 0; // 去除容差范围内的微小轴向分量。
    forceDirection_ /= mag(forceDirection_); // 归一化受力方向；向量长度不用于放大或缩小力。

    // execute 每步检查独立流场时钟，因此必须保持每时间步调用。
    if // 开始条件判断，条件在后面的括号中展开。
    ( // 开始上一行声明或表达式的多行括号内容。
        dict.lookupOrDefault<word>("executeControl", "timeStep") != "timeStep" // 检查 executeControl 是否被设置成每时间步以外的方式。
     || dict.lookupOrDefault<label>("executeInterval", 1) != 1 // 检查 executeInterval 是否不等于 1，防止漏检独立流场时钟。
    ) // 结束当前参数表、条件表达式或分组表达式。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalIOErrorInFunction(dict) // 为不合要求的执行频率创建致命配置错误。
            << "Use executeControl timeStep; executeInterval 1; " // 提示执行钩子必须每个时间步调用一次。
            << "Set series frequency with writeControl/writeInterval, and " // 提示时间序列频率应由 writeControl 和 writeInterval 指定。
            << "field frequency with fieldWriteControl/fieldWriteInterval." // 提示流场频率应由 fieldWriteControl 和 fieldWriteInterval 指定。
            << exit(FatalIOError); // 终止运行，要求修正执行频率配置。
    } // 结束当前命名空间、函数体或条件/循环代码块。

    forAll(wallID_, walli) // 依次检查用户列出的所有目标壁面。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        const label patchi = mesh_.boundaryMesh().findPatchID(wallID_[walli]); // 按壁面名称查找本处理器网格中的 patch 索引。
        if // 开始条件判断，条件在后面的括号中展开。
        ( // 开始上一行声明或表达式的多行括号内容。
            patchi < 0 // 检查是否没有找到这个名称的 patch。
         || !isA<wallPolyPatch>(mesh_.boundaryMesh()[patchi]) // 若名称存在，再检查它是否属于 wallPolyPatch 壁面类型。
        ) // 结束当前参数表、条件表达式或分组表达式。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            FatalIOErrorInFunction(dict) // 创建壁面配置无效的致命输入错误。
                << "wallID entry " << wallID_[walli] // 在错误信息中写入有问题的壁面名称。
                << " must name an existing wall patch." << exit(FatalIOError); // 说明该名称必须对应已有壁面，并终止运行。
        } // 结束当前命名空间、函数体或条件/循环代码块。
        for (label j = 0; j < walli; ++j) // 把当前壁面名与它之前的所有壁面名逐一比较。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            if (wallID_[j] == wallID_[walli]) // 检查同一个壁面是否被用户重复列出。
            { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
                FatalIOErrorInFunction(dict) // 创建重复壁面名称的致命输入错误。
                    << "Duplicate wallID " << wallID_[walli] // 输出重复出现的 wallID 名称。
                    << exit(FatalIOError); // 终止运行，防止重复积分和重复写行。
            } // 结束当前命名空间、函数体或条件/循环代码块。
        } // 结束当前命名空间、函数体或条件/循环代码块。
    } // 结束当前命名空间、函数体或条件/循环代码块。

    const dictionary controls(fieldControls(dict)); // 获取补齐缺省条目后的独立流场输出配置。
    const word control(controls.get<word>("fieldWriteControl")); // 读取流场输出控制方式，例如 timeStep 或 runTime。
    if // 开始条件判断，条件在后面的括号中展开。
    ( // 开始上一行声明或表达式的多行括号内容。
        control != "none" && control != "timeStep" // 先检查控制方式是否既不是 none 也不是 timeStep。
     && control != "writeTime" && control != "runTime" // 再排除 writeTime 和 runTime；四种均不匹配时视为无效。
    ) // 结束当前参数表、条件表达式或分组表达式。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalIOErrorInFunction(dict) // 为不支持的流场输出方式创建致命输入错误。
            << "fieldWriteControl supports none, timeStep, writeTime, runTime." // 列出允许使用的四种流场输出方式。
            << exit(FatalIOError); // 终止运行，要求选择受支持的流场输出方式。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    const scalar interval(controls.get<scalar>("fieldWriteInterval")); // 读取流场输出间隔，先按标量处理以支持 runTime 的小数间隔。
    if // 开始条件判断，条件在后面的括号中展开。
    ( // 开始上一行声明或表达式的多行括号内容。
        interval <= 0 // 检查流场输出间隔是否非正。
     || ((control == "timeStep" || control == "writeTime") // 对于 timeStep 或 writeTime，额外要求间隔为整数。
      && interval != scalar(label(interval))) // 转换成整数再转回标量，检查是否存在不允许的小数部分。
    ) // 结束当前参数表、条件表达式或分组表达式。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalIOErrorInFunction(dict) // 为无效的输出间隔创建致命输入错误。
            << "fieldWriteInterval must be positive (an integer for " // 提示间隔必须为正，并开始说明整数限制。
            << "timeStep/writeTime)." << exit(FatalIOError); // 补充 timeStep 和 writeTime 的整数要求，然后终止运行。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    fieldWriteControl_.read(controls); // 让独立流场时钟读取经过检查的设置。
    results_.setSize(wallID_.size()); // 按目标壁面数分配结果行，每个壁面对应九个数值。
    lastCalculationIndex_ = -1; // 使旧的积分缓存失效，下次写序列时按新配置重新计算。
    // 重新读取字典时保留防重复写出状态和原来的启动时刻目录。
    return true; // 返回 true，表示字典读取和有效性检查成功。
} // 结束当前命名空间、函数体或条件/循环代码块。

bool Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::calc // 定义 calc：计算辅助场和所有力元积分，并按参数决定是否写场。
( // 开始上一行声明或表达式的多行括号内容。
    const bool outputFields // 接收流场输出标志；true 写场，false 只计算积分。
) // 结束当前参数表、条件表达式或分组表达式。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    const volVectorField& U = lookupObject<volVectorField>(fieldName_); // 直接引用求解器内存中当前的速度场，不从磁盘读取 U。
    if (U.dimensions() != dimVelocity) // 检查输入场的量纲是否为速度，即长度除以时间。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalErrorInFunction << "field must have velocity dimensions." // 量纲错误时输出致命错误信息。
            << exit(FatalError); // 终止运行，避免用非速度场计算力元贡献。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    // const turbulenceModel& turbModel = // 声明对求解器已有通用湍流模型的只读引用。
    //     lookupObject<turbulenceModel>(turbulenceModel::propertiesName); // 按标准属性名从注册表查找模型；层流也需建立该模型接口。
    const turbulenceModel* turbModelPtr = nullptr; // 模型可选：icoFoam 不创建此对象。
    dimensionedScalar nuLaminar("nu", dimViscosity, scalar(0)); // 保存无湍流模型时使用的层流运动黏度。
    if (mesh_.foundObject<turbulenceModel>(turbulenceModel::propertiesName)) // 检查求解器是否创建了模型对象。
    { // 已有模型时保留原来的有效黏度处理。
        turbModelPtr = &mesh_.lookupObject<turbulenceModel>(turbulenceModel::propertiesName); // 保存已有模型的只读指针。
    } // 完成模型分支。
    else // 无模型时兼容 icoFoam 的常运动黏度层流算例。
    { // 开始从求解器已注册的物性字典读取 nu。
        const IOdictionary& transport = mesh_.lookupObject<IOdictionary>("transportProperties"); // 引用 icoFoam 创建的字典对象。
        nuLaminar = dimensionedScalar("nu", dimViscosity, transport); // 按 L²/T 量纲读取 nu。
    } // 完成层流分支。

    volVectorField vorticity(fieldIO("VorticityFET"), fvc::curl(U)); // 计算涡量 omega=curl(U)，并创建名为 VorticityFET 的辅助场。
    volVectorField lambVector(fieldIO("LambVectorFET"), vorticity ^ U); // 计算 Lamb 向量 omega 叉乘 U，叉乘在 OpenFOAM 中写为 ^。

    volScalarField Potential // 开始创建量纲为 L²/T 的圆柱平移速度势标量场 Potential。
    ( // 开始上一行声明或表达式的多行括号内容。
        fieldIO("Potential"), mesh_, dimensionedScalar("zero", dimLength*dimVelocity, 0) // 将速度势初始化为零，量纲 L²/T，对应 OpenFOAM 的 [0 2 -1 0 0 0 0]。
    ); // 结束当前函数调用或对象构造语句。
    volVectorField GradPotential // 创建当前方向速度势的完整解析梯度矢量场，量纲为速度。
    ( // 开始完整梯度场的构造参数。
        fieldIO("GradPotential"), mesh_, // 设置完整梯度的场名、网格及显式写出属性。
        dimensionedVector("zero", dimVelocity, vector::zero) // 将完整速度势梯度初始化为零矢量，量纲 L/T。
    ); // 完成完整梯度场的构造。

    const scalar radiusSqr = sqr(radius_.value()); // 计算圆柱半径平方 R²，避免重复计算。
    const scalar minRadiusSqr = max(VSMALL, SMALL*radiusSqr); // 设置接近圆心时的奇点检查阈值，量级随 R² 缩放。
    forAll(mesh_.C(), celli) // 遍历本处理器的所有单元中心。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        const vector d(mesh_.C()[celli] - cylinderCentre_); // 计算单元中心相对于圆柱中心的位移向量。
        const scalar rSqr = sqr(d.x()) + sqr(d.y()); // 计算到 z 向圆柱轴的径向距离平方 r²=X²+Y²。
        const scalar q = forceDirection_ & d; // 计算单位受力方向与位置矢量的点积 q=e_x*X+e_y*Y。
        if (rSqr <= minRadiusSqr) // 检查当前单元中心是否过于接近势函数的奇点。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            FatalErrorInFunction << "Cell at the cylinder-potential singularity." // 报告有单元位于圆柱辅助势奇点附近。
                << exit(FatalError); // 终止运行，避免除零或无意义的大数值。
        } // 结束当前命名空间、函数体或条件/循环代码块。
        Potential[celli] = potentialSpeed_.value()*radiusSqr*q/rSqr; // 计算有量纲圆柱平移速度势 Phi=Vref*R²*q/r²，单位 m²/s。
        GradPotential[celli] = -potentialSpeed_.value()*radiusSqr* // 准备赋入辅助势的完整解析梯度。
        ( // 开始解析梯度矢量表达式。
            2*q*vector(d.x(), d.y(), 0)/sqr(rSqr) // 计算梯度中的 2R²*q*(X,Y,0)/r⁴ 部分。
          - forceDirection_/rSqr // 减去 R²*e/r²，得到完整的 gradPhi。
        ); // 完成当前单元解析梯度的赋值。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    // 在实际边界面中心计算解析值；同时处理并行分区间的边界。
    forAll(mesh_.Cf().boundaryField(), patchi) // 遍历所有边界 patch，包括并行分区间的 processor patch。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        if (!isA<wallPolyPatch>(mesh_.boundaryMesh()[patchi])) // 检查当前 patch 是否属于壁面类型。
        { // 当前 patch 不是壁面。
            continue; // 跳过它，处理下一个 patch。
        } // 结束类型判断。
        const vectorField& centres =
            mesh_.Cf().boundaryField()[patchi]; // 取得当前壁面的面心坐标。
        forAll(centres, facei) // 遍历当前 patch 的每一个边界面。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            const vector d(centres[facei] - cylinderCentre_); // 计算边界面中心相对于圆柱中心的位移。
            const scalar rSqr = sqr(d.x()) + sqr(d.y()); // 计算该边界面中心到圆柱轴的距离平方。
            const scalar q = forceDirection_ & d; // 计算单位受力方向与位置矢量的点积 q=e_x*X+e_y*Y。
            if (rSqr <= minRadiusSqr) // 检查边界面中心是否位于势函数的奇点附近。
            { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
                FatalErrorInFunction // 创建边界面奇点的致命错误信息。
                    << "Boundary face at the cylinder-potential singularity." // 指出有边界面中心过于接近圆柱辅助势的奇点。
                    << exit(FatalError); // 终止运行，避免在边界面上除零。
            } // 结束当前命名空间、函数体或条件/循环代码块。
            Potential.boundaryFieldRef()[patchi][facei] = potentialSpeed_.value()*radiusSqr*q/rSqr; // 按相同受力方向计算当前边界面中心的解析辅助势。
            GradPotential.boundaryFieldRef()[patchi][facei] = -potentialSpeed_.value()*radiusSqr* // 准备赋入边界面中心的完整解析梯度。
            ( // 开始边界面中心的解析梯度表达式。
                2*q*vector(d.x(), d.y(), 0)/sqr(rSqr) // 计算边界梯度中的 2R²*q*(X,Y,0)/r⁴ 部分。
            - forceDirection_/rSqr // 减去 R²*e/r²，得到当前边界面的完整梯度。
            ); // 完成当前边界面解析梯度的赋值。
        } // 结束当前命名空间、函数体或条件/循环代码块。
    } // 结束当前命名空间、函数体或条件/循环代码块。

    // 各矢量积分核统一与完整的方向辅助势梯度点乘。
    volScalarField flow // 开始创建体积流动结构贡献场。
    ( // 开始上一行声明或表达式的多行括号内容。
        fieldIO("FlowStructureContribution"), // 设置流动结构贡献场的输出名称和读写属性。
        (lambVector & GradPotential / potentialSpeed_) // 计算完整的 Lamb 向量与辅助势梯度的点积，包含 x、y 两项。
    ); // 结束当前函数调用或对象构造语句。
    // 面牵引贡献场具有压力量纲；只给目标壁面赋值，内部单元保持零。
    const dimensionedScalar zeroPressure("zero", dimPressure, 0); // 定义具有压力量纲的零值，用于初始化面牵引贡献场。
    volScalarField wallVorticity // 开始创建壁面涡量贡献场。
    ( // 开始上一行声明或表达式的多行括号内容。
        fieldIO("WallVorticityContribution"), mesh_, zeroPressure // 在当前网格上建立壁面涡量贡献场，初始各处为零。
    ); // 结束当前函数调用或对象构造语句。
    volScalarField wallShearStress // 开始创建原程序的无滑移壁面剪切贡献场。
    ( // 开始上一行声明或表达式的多行括号内容。
        fieldIO("WallShearStressContribution"), mesh_, zeroPressure // 设置无滑移剪切贡献场的名称、网格及零压力初值。
    ); // 结束当前函数调用或对象构造语句。
    volScalarField wallVelocity // 开始创建壁面速度贡献场。
    ( // 开始上一行声明或表达式的多行括号内容。
        fieldIO("WallVelocityContribution"), mesh_, zeroPressure // 设置壁面速度贡献场的名称、网格及零压力初值。
    ); // 结束当前函数调用或对象构造语句。

    scalar flowSum = 0, negativeSum = 0, positiveSum = 0; // 把流动结构总和、负贡献、正贡献全部清零。
    forAll(flow, celli) // 遍历本处理器的全部体积贡献场单元。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        const scalar contribution = rho_.value()*flow[celli]*mesh_.V()[celli]; // 计算当前单元的力贡献：常密度乘贡献场再乘单元体积。
        flowSum += contribution; // 累加当前单元的流动结构贡献。
        if (contribution < 0) negativeSum += contribution; // 若当前单元贡献为负，把它计入负贡献总和。
        else positiveSum += contribution; // 否则把当前单元贡献计入正贡献总和。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    reduce(flowSum, sumOp<scalar>()); // 对所有处理器的流动结构积分求全局和。
    reduce(negativeSum, sumOp<scalar>()); // 对所有处理器的负流动结构贡献求全局和。
    reduce(positiveSum, sumOp<scalar>()); // 对所有处理器的正流动结构贡献求全局和。

    forAll(wallID_, walli) // 逐个目标壁面计算面贡献，并准备对应的输出结果行。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        const label patchi = mesh_.boundaryMesh().findPatchID(wallID_[walli]); // 重新按壁面名称查找 patch 索引。
        if (patchi < 0) // 检查目标壁面是否在网格中消失。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            FatalErrorInFunction << "Wall disappeared: " << wallID_[walli] // 在错误信息中写出消失的壁面名称。
                << exit(FatalError); // 终止运行，避免访问不存在的 patch。
        } // 结束当前命名空间、函数体或条件/循环代码块。
        resultRow& row = results_[walli]; // 引用当前壁面对应的九列结果，可直接写入缓存。
        forAll(row, j) row[j] = 0; // 把该壁面的九列结果全部清零。
        row[0] = flowSum; // 结果第 0 项保存全域流动结构积分。
        row[1] = negativeSum; // 结果第 1 项保存全域负流动结构贡献。
        row[2] = positiveSum; // 结果第 2 项保存全域正流动结构贡献。

        const fvPatch& patch = mesh_.boundary()[patchi]; // 引用当前壁面的有限体积 patch，读取面几何信息。
        // const tmp<scalarField> tNuEff = turbModel.nuEff(patchi); // 获取当前壁面有效运动黏度的临时场，保持其生命周期。
        const tmp<scalarField> tNuEff // 保持边界黏度临时场的生命周期。
        ( // 根据模型是否存在选择有效运动黏度。
            turbModelPtr // 检查是否已有模型指针。
          ? turbModelPtr->nuEff(patchi) // 有模型时读取壁面 nuEff，适用于原有模型路径。
          : tmp<scalarField>(new scalarField(patch.size(), nuLaminar.value())) // icoFoam 层流时每个面使用相同的 nu。
        ); // 完成当前壁面黏度场构造。
        const scalarField& nuEff = tNuEff(); // 引用临时对象中的有效运动黏度数组，避免额外复制。
        forAll(patch, facei) // 遍历当前目标壁面的所有面。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            const scalar area = mag(patch.Sf()[facei]); // 取当前边界面的面积，即面积向量的模。
            const vector n(patch.Sf()[facei]/area); // 把面积向量归一化，得到由流体域向外的单位法向 n。
            const vector& Uf = U.boundaryField()[patchi][facei]; // 读取当前壁面面的速度矢量 Uf。
            const vector& omega = vorticity.boundaryField()[patchi][facei]; // 读取当前壁面面的涡量矢量 omega。
            const vector& gradPhi = GradPotential.boundaryField()[patchi][facei]/potentialSpeed_.value(); // 引用当前壁面面上的完整辅助势梯度矢量。
            const scalar muEff = rho_.value()*nuEff[facei]; // 用 rho*nuEff 得到有效动力黏度 muEff。

            const scalar vort = muEff*((n ^ omega) & gradPhi); // 用完整梯度对法向叉乘涡量做点积，得到当前方向的壁面涡量项。
            const scalar shearstress = muEff*((n ^ omega) & forceDirection_); // 按原积分核把 muEff*(n 叉乘 omega)投影到受力方向。
            const scalar velocity = 0.5*rho_.value()*(n & forceDirection_)*magSqr(Uf); // 计算 rho*(n 点乘 gradPhi)*|Uf|²/2，保留完整法向投影。

            wallVorticity.boundaryFieldRef()[patchi][facei] = vort; // 将涡量项写入当前壁面面的辅助场值。
            wallShearStress.boundaryFieldRef()[patchi][facei] = shearstress; // 将无滑移剪切项写入当前壁面面的辅助场值。
            wallVelocity.boundaryFieldRef()[patchi][facei] = velocity; // 将壁面速度项写入当前壁面面的辅助场值。
            row[3] += vort*area; // 面涡量项乘面积后，累加到结果第 3 项。
            row[4] += shearstress*area; // 壁面切应力项乘面积后，累加到结果第 4 项。
            row[5] += velocity*area; // 壁面速度项乘面积后，累加到结果第 5 项。
        } // 结束当前命名空间、函数体或条件/循环代码块。
        for (label j = 3; j <= 5; ++j) // 遍历面贡献所在的结果索引区间 3 到 5。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
           reduce(row[j], sumOp<scalar>()); // 对面贡献做全局并行求和。
        } // 结束当前命名空间、函数体或条件/循环代码块。
    } // 结束当前命名空间、函数体或条件/循环代码块。

    if (outputFields) // 仅在本次调用要求输出流场时进入写场分支。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        // 每个处理器使用 OpenFOAM 标准 I/O 写出自己分区中的场。
        bool ok = true; // 初始化写出成功标志，随后累计所有场的写出状态。
        ok = vorticity.write() && ok; // 写涡量场，并保留此前任何一次写出失败的状态。
        ok = lambVector.write() && ok; // 写 Lamb 向量场，并累计写出状态。
        ok = Potential.write() && ok; // 写解析辅助势场 Potential，并累计写出状态。
        ok = GradPotential.write() && ok; // 写出完整辅助势梯度矢量场，并累计写出状态。
        ok = flow.write() && ok; // 写体积流动结构贡献场，并累计写出状态。
        ok = wallVorticity.write() && ok; // 写壁面涡量贡献场，并累计写出状态。
        ok = wallShearStress.write() && ok; // 写无滑移剪切贡献场，并累计写出状态。
        ok = wallVelocity.write() && ok; // 写壁面速度贡献场，并累计写出状态。
        if (!ok) // 检查是否至少有一个辅助场写出失败。
        { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
            FatalErrorInFunction << "Failed to write force-element fields." // 报告辅助场写出失败。
                << exit(FatalError); // 终止运行，避免忽略文件写出错误。
        } // 结束当前命名空间、函数体或条件/循环代码块。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    lastCalculationIndex_ = mesh_.time().timeIndex(); // 记录当前已完成计算的时间步索引，使同一步的序列输出可以复用积分。
    return true; // 返回计算成功。
} // 结束当前命名空间、函数体或条件/循环代码块。

bool Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::execute() // 定义 execute：每步检查独立流场输出时钟。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    const label index = mesh_.time().timeIndex(); // 读取当前时间步的整数索引，它不同于实际物理时间值。
    if (lastFieldCheckIndex_ == index) return true; // 若本时间步已经检查过流场时钟，则立即返回，避免重复计数。
    lastFieldCheckIndex_ = index; // 记录本时间步已经执行过流场时钟检查。
    if (writeFields_ && fieldWriteControl_.execute()) return calc(true); // 仅在启用写场且流场时钟触发时，计算积分并写出辅助场。
    return true; // 未触发写场时直接返回成功，不做额外计算。
} // 结束当前命名空间、函数体或条件/循环代码块。

bool Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::writeSeries() // 定义 writeSeries：由主处理器把结果追加到时间序列文件。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    if (!Pstream::master()) return true; // 非主处理器立即返回，避免多个进程同时写同一个汇总文件。

    fileName outputDir(mesh_.time().globalPath()/"postProcessing"/name()); // 在算例全局路径下创建 postProcessing/对象名 这一基础路径。
    if (mesh_.name() != polyMesh::defaultRegion) // 检查当前网格是否属于非默认区域。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        outputDir /= mesh_.name(); // 多区域情况下增加区域名子目录，以区分不同网格的结果。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    outputDir /= outputStartTime_; // 追加固定的本次启动时刻目录，例如初算 0 或续算 20。
    mkDir(outputDir); // 创建输出目录及所需父目录。
    const fileName dataFile(outputDir/"force_FEM.dat"); // 把汇总数据文件名固定为 force_slip.dat。
    // 使用追加模式保留已有时间序列，避免每次写出截断文件。
    std::ofstream file(dataFile.c_str(), std::ios::out | std::ios::app); // 以追加模式打开文件；已有文件不会被截断或覆盖。
    if (!file.good()) // 检查文件输出流是否成功打开。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalErrorInFunction << "Cannot open " << dataFile // 打开失败时报告出错文件的完整路径。
            << exit(FatalError); // 终止运行，防止静默丢失时间序列数据。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    if (file.tellp() == std::streampos(0)) // 检查当前追加位置是否为 0；只有空文件才写列标题。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        file << "\"Time\",\"wall\",\"SumFlowStructureContribution\"," // 开始写 CSV 表头：时间、壁面名、全域流动结构积分。
             << "\"SumNegativeFlowStructure\",\"SumPositiveFlowStructure\"," // 继续写全域负流动结构与正流动结构贡献的列名。
             << "\"SumWallVorticityContribution\"," // 写壁面涡量贡献列名。
             << "\"SumWallShearStressContribution\"," // 写无滑移剪切贡献列名。
             << "\"SumWallVelocityContribution\"\n"; // 写壁面速度贡献列名。

    } // 结束当前命名空间、函数体或条件/循环代码块。
    file << std::scientific << std::setprecision(precision_); // 采用科学计数法输出，每个数的小数位数由 precision_ 指定。
    forAll(wallID_, walli) // 依次为各个目标壁面追加一行结果。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        file << mesh_.time().value() << ',' << wallID_[walli]; // 先写当前实际仿真时间和壁面名称，中间用逗号分隔。
        forAll(results_[walli], j) file << ',' << results_[walli][j]; // 按缓存顺序写出该壁面的九列数值，每列前加一个逗号。
        file << '\n'; // 写入换行符，结束当前壁面的这一行。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    file.flush(); // 立即把流缓冲区刷新到文件，便于仿真中查看最新结果。
    if (!file.good()) // 检查刷新和写入操作是否失败。
    { // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
        FatalErrorInFunction << "Failed to append " << dataFile // 报告追加数据失败的文件路径。
            << exit(FatalError); // 终止运行，避免把写出失败当成成功。
    } // 结束当前命名空间、函数体或条件/循环代码块。
    return true; // 返回时间序列写出成功；局部文件流在函数结束时自动关闭。
} // 结束当前命名空间、函数体或条件/循环代码块。

bool Foam::functionObjects::ForceElementTheoryForAlterRotatingCylinder::write() // 定义 write：标准 writeControl 触发的时间序列输出入口。
{ // 开始上一行所定义的命名空间、函数体或条件/循环代码块。
    const label index = mesh_.time().timeIndex(); // 读取当前时间步索引，用于识别重复调用。
    if (lastSeriesIndex_ == index) return true; // 同一时间步已经写过序列时直接返回，防止重复追加。
    // 若本时间步已计算并写过流场，直接复用对应的积分缓存。
    if (lastCalculationIndex_ != index && !calc(false)) return false; // 若本时间步尚未计算则只计算积分；计算失败时返回 false。
    if (!writeSeries()) return false; // 追加时间序列；写出函数返回失败时把失败状态继续传回。
    lastSeriesIndex_ = index; // 记录本时间步已写入时间序列。
    return true; // 返回本次时间序列输出成功。
} // 结束当前命名空间、函数体或条件/循环代码块。


