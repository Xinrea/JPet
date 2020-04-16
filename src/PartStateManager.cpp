#include "PartStateManager.h"
#include <Id/CubismIdManager.hpp>

namespace {
    PartStateManager* s_instance = NULL;
}

PartStateManager* PartStateManager::GetInstance() {
    if (s_instance == NULL) {
        s_instance = new PartStateManager();
    }
    return s_instance;
}

void PartStateManager::SetModel(CubismModel* m)
{
	model = m;
	for (int i = 0; i < ParamNum; i++)
	{
		ParamIdList.push_back(CubismFramework::GetIdManager()->GetId(ParamList[i]));
	}
}

void PartStateManager::SaveState()
{
	if (model == nullptr) return;
	for (int i = 0; i < ParamNum; i++) {
		ParamState[ParamList[i]] = model->GetParameterValue(ParamIdList[i]);
	}
}

float PartStateManager::GetState(string param)
{
	if (model == nullptr) return 0;
	else return ParamState[param];
}

void PartStateManager::SetState()
{
	if (model == nullptr) return;
	for (int i = 0; i < ParamNum; i++) {
		model->SetParameterValue(ParamIdList[i], ParamState[ParamList[i]]);
	}
}