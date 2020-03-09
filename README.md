# JPet

桌面宠物轴伊

![img](screenshots/JPet.gif)

## Live2d 模型

参考：轴伊Joi

绘制：Xinrea

制作：Xinrea






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

- 更多交互动画
- ~~配置保存~~
- ~~快捷打开直播间/主页~~
- 自定义图标
- 快捷查看实时粉丝数
- 直播通知
- 托盘图标和菜单
- ~~拖放删除文件~~
- ~~自定义缩放参数~~
- 开机自启动
- 打包安装程序
- ...

## Live2D Open Software License

Live2D Cubism Components is available under Live2D Open Software License.

* [Live2D Open Software License Agreement](https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html)
* [Live2D Open Software 使用許諾契約書](https://www.live2d.com/eula/live2d-open-software-license-agreement_jp.html)


## Live2D Proprietary Software License

Live2D Cubism Core is available under Live2D Proprietary Software License.

* [Live2D Proprietary Software License Agreement](https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html)
* [Live2D Proprietary Software 使用許諾契約書](https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_jp.html)