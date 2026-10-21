# 验证记录

整理日期：2026-10-21。

原始 `.C`、`.H`、`Make/files`、`Make/options` 未修改；使用 SHA-256 与来源逐项比对，校验清单见 `original-files.sha256`。

## 已完成的检查

- 四个原始文件与来源文件按字节及 SHA-256 比对一致。
- `Allwmake`、`Allwclean` 通过 shell 语法检查。
- 在独立副本中执行 `sh ./Allwmake`，完成编译及共享库链接，退出码为 0。
- 编译环境：WSL2 / Ubuntu 20.04、OpenCFD OpenFOAM v2306、GCC 9.4.0，`linux64GccDPInt32Opt`。
- 生成库：`libfieldFunctionObjects_USER_FEMAlterRotatingCylinder.so`；编译产物未纳入仓库。
- 上传文件清单检查、常见凭据模式检查和新增文件空白检查通过。

## 验证范围

本次没有修改原始源文件或 Make 配置。原有换行与文件末尾空行也予以保留。

本次完成了原样源码的编译与链接验证，没有运行 CFD 算例、并行计算、配置片段运行测试或物理精度验证。`examples/functions.dict` 为使用格式示意，不是完整验证算例。其他 OpenFOAM 版本的兼容性未测试。
