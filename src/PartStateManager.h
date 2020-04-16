#pragma once
#pragma once
#include <CubismFramework.hpp>
#include "Model/CubismModel.hpp"
#include "Utils/CubismJson.hpp"
#include <map>
#include <string>
#include <vector>
using std::map;
using std::string;
using std::vector;
using namespace Live2D::Cubism::Framework;

class PartStateManager
{
	vector<CubismIdHandle> ParamIdList;
	map<string, float> ParamState;
	CubismModel* model = nullptr;
public:
	const int ParamNum = 17;
	const char* ParamList[17] = {
		"ParamClothes",
		"ParamQuan",
		"ParamStar",
		"ParamVeye",
		"ParamMouth1",
		"ParamMouth2",
		"ParamMouth3",
		"ParamMouth4",
		"ParamMouth5",
		"Param",
		"ParamGun",
		"ParamHat",
		"ParamKneeSocks",
		"ParamEarl",
		"ParamEarr",
		"ParamLegBelt",
		"ParamSleeveShow"
	};
	const float ParamDefault[17] = {
		0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0
	};
	static PartStateManager* GetInstance();
	void SetModel(CubismModel* m);
	void SaveState();
	float GetState(string param);
	void SetState();
	void ImportState(map<string, float> State) { ParamState = State; }
	map<string, float> GetAllState() { return ParamState; }
};



/*
共 20 个参数
整体部分：
ParamClothes - 新旧衣服整体切换：0 -> 1 | 新 -> 旧

眼睛部分：
ParamQuan	 - 圈圈眼：0 -> 1
ParamStar	 - 星星眼：0 -> 1
ParamVeye	 - V字眼：0 -> 1

嘴部：
ParamMouth1  - 冷漠
ParamMouth2  - 张开
ParamMouth3  - 不开心
ParamMouth4  - 三角
ParamMouth5  - 慌乱
Param		 - 吐舌: -30 -> 30

其他部位：
ParamGun     - 枪
ParamHat     - 帽子
ParamKneeSocks - 袜子
ParamEarl    - 左耳
ParamEarr    - 右耳
ParamLegBelt - 腿环
ParamSleeveShow - 袖子
*/


