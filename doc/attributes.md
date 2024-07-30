# 数值设定 (draft)

桌宠拥有 `速度`、`耐力`、`力量`、`毅力`和`智力`共五种属性，以及最基础的经验值设定，显然属性的设定参考自赛马娘。由于桌宠不会有赛马娘那样丰富的游戏内容，因此属性的作用会有所简化，本文档将对各属性进行详细的说明。

## 经验值 EXP

先简要介绍一下经验值的设定，由于桌宠主要是处于挂机状态，因此经验值是通过挂机来获取的，基础设定为每分钟获取 1 点，可通过增强属性等方式提升。

## 属性值 Attribute

各种属性值的作用将会在后文详细描述。此处简要介绍属性值的获取方式，主要设计为三种方式：

1. 直接兑换：每 10 点经验可以直接兑换任意 1 点属性，每次兑换后所需的经验值变为 1.5 倍；希望控制在能够直接兑换 20-30 点属性。
2. 任务获取：每种属性会有对应的一系列任务，总计约 50 点。
3. 道具获取：5000 点经验兑换某个道具，有 10% 的概率增加一点属性。
4. 临时效果：完成各种任务可以获取道具，某些道具使用后可以获取临时的属性提升。

## 智力 INTELLECT

影响经验获取的效率。经验值的基础设定是每挂机一分钟获取一点，以下讨论时所提到的经验获取量均为每分钟获取的值。

接下来设定智力与经验获取量的函数，已知初始智力 1 点对应初始经验获取量 1 点；且期望智力加满（100 点）时，经验获取量增加到 100 点。为了趣味性，智力与经验获取量不为线性关系。

设计如下分段函数：

$$s(x)=30*(1-(1-0.03x)^3)\quad x\le69$$
$$s(x)=100*(1-0.5(-0.03x+2.94)^3)\quad x>69$$

函数图像可见 [Desmos](https://www.desmos.com/calculator/abmzllpxac)：

![graph](./desmos-graph.png)

那么对于一个速度值 p，可计算其对应的经验获取量：

$$exp(p)=1+99*\frac{s(p)}{100}$$

## 毅力 WILLPOWER

影响任务的成功率。当属性达标时，一项任务的成功率通常为 80%，并非 100% 成功，此时需要毅力属性来进行补正。毅力达到 40 点即可让 80% 成功率的任务补正为 100%；也就是说，当毅力达到 100 点时，可提供高达 50% 的补正。

平均下来，每点毅力提供 0.5% 的成功率补正（由于本身影响的是成功率，带有随机因素，因此不必使用缓动函数来使其变复杂）。

当对于某任务属性不达标时，属性每差一点，成功率下降 2%。例如：

任务要求属性耐力 50 点/力量 50 点，但是目前属性为耐力 40 点/力量 45 点，那么任务成功率为：
80% - 10\*2% - 5\*2% = 50%

同时，当任意属性相比任务要求缺少超过 20 点时，任务成功率变为 0%；特别的，当任务成功率本身为 0% 时，毅力无法进行补正。

## 速度 SPEED

影响任务的所需时长。例如某任务完成需要 10 分钟，通过提升速度可以缩短这一时间。期望速度达到 100 点时，任务所需时间可以缩短 90%。同样的，借用智力属性中提到的 s(x) 来实现这一点：

$$t(x) = T(1-0.9\frac{s(x)}{100})$$

## 耐力 ENDURANCE & 力量 STRENGTH

无特殊作用。某些任务会需求这些属性值，不达标任务成功率会下降。