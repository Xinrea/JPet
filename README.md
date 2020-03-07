# JPet

桌面宠物轴伊

![img](screenshots/JPet.gif)

## 编译

该项目需要以下依赖

 - GLEW_VERSION=2.1.0

 - GLFW_VERSION=3.3.2

 - CubismSdkForNative-4-r.1 -  Core, Framework

 - Visual Studio 2019 - v142 Toolset # 配置见CMakeLists.txt

将glew和glfw置于`thirdParty`下;

将Core和Framework置于项目目录下

然后

```
cmake .
```

用Visual Studio 2019打开生成的`JPet.sln`即可

## TODO

- UI界面
- 模型预置动画
- 配置保存
- 快捷打开直播间/主页
- 快捷查看实时粉丝数
- 直播通知
- ...
