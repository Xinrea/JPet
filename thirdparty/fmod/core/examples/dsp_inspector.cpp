/*==============================================================================
Plug-in Inspector Example
Copyright (c), Firelight Technologies Pty, Ltd 2004-2024.

This example shows how to enumerate loaded plug-ins and their parameters.

For information on using FMOD example code in your own programs, visit
https://www.fmod.com/legal
==============================================================================*/
#include "fmod.hpp"
#include "common.h"

const int   INTERFACE_UPDATETIME = 50;      // 50ms update for interface
const int   MAX_PLUGINS_IN_VIEW = 5;
const int   MAX_PARAMETERS_IN_VIEW = 14;

enum InspectorState
{
    PLUGIN_SELECTOR,
    PARAMETER_VIEWER
};

struct PluginSelectorState
{
    FMOD::System *system;
    int numplugins;
    int cursor;
};

struct ParameterViewerState
{
    FMOD::DSP *dsp;
    int numparams;
    int scroll;
};

void drawTitle()
{
    Common_Draw("==================================================");
    Common_Draw("Plug-in Inspector Example.");
    Common_Draw("Copyright (c) Firelight Technologies 2004-2024.");
    Common_Draw("==================================================");
    Common_Draw("");
}

bool hasDataParameter(const FMOD_DSP_DESCRIPTION *desc, FMOD_DSP_PARAMETER_DATA_TYPE type)
{
    for (int i = 0; i < desc->numparameters; i++)
    {
        if (desc->paramdesc[i]->type == FMOD_DSP_PARAMETER_TYPE_DATA && ((type >= 0 && desc->paramdesc[i]->datadesc.datatype >= 0) || desc->paramdesc[i]->datadesc.datatype == type))
        {
            return true;
        }
    }

    return false;
}

void drawDSPInfo(const FMOD_DSP_DESCRIPTION *desc)
{
    Common_Draw("Name (Version) : %s (%x)", desc->name, desc->version);
    Common_Draw("SDK Version    : %d", desc->pluginsdkversion);
    Common_Draw("Type           : %s", desc->numinputbuffers ? "Effect" : "Sound Generator");
    Common_Draw("Parameters     : %d", desc->numparameters);
    Common_Draw("Audio Callback : %s", desc->process ? "process()" : "read()");
    Common_Draw("");
    Common_Draw(" Reset | Side-Chain | 3D | Audibility | User Data");
    Common_Draw("   %s  |     %s     | %s |     %s     |     %s   ",
        desc->reset ? "Y " : "--",
        hasDataParameter(desc, FMOD_DSP_PARAMETER_DATA_TYPE_SIDECHAIN) ? "Y " : "--",
        hasDataParameter(desc, FMOD_DSP_PARAMETER_DATA_TYPE_3DATTRIBUTES) || hasDataParameter(desc, FMOD_DSP_PARAMETER_DATA_TYPE_3DATTRIBUTES_MULTI) ? "Y " : "--",
        hasDataParameter(desc, FMOD_DSP_PARAMETER_DATA_TYPE_OVERALLGAIN) ? "Y " : "--",
        hasDataParameter(desc, FMOD_DSP_PARAMETER_DATA_TYPE_USER) || desc->userdata ? "Y " : "--");
}

void drawDSPList(PluginSelectorState *state)
{
    unsigned int pluginhandle;
    char         pluginname[256];
    FMOD_RESULT  result;

    Common_Draw("Press %s to select the next plug-in", Common_BtnStr(BTN_DOWN));
    Common_Draw("Press %s to select the previous plug-in", Common_BtnStr(BTN_UP));
    Common_Draw("Press %s to view the plug-in parameters", Common_BtnStr(BTN_RIGHT));
    Common_Draw("");

    int start = Common_Clamp(0, state->cursor - (MAX_PLUGINS_IN_VIEW - 1) / 2, state->numplugins - MAX_PLUGINS_IN_VIEW);
    for (int i = start; i < start + MAX_PLUGINS_IN_VIEW; i++)
    {
        result = state->system->getPluginHandle(FMOD_PLUGINTYPE_DSP, i, &pluginhandle);
        ERRCHECK(result);

        result = state->system->getPluginInfo(pluginhandle, 0, pluginname, 256, 0);
        ERRCHECK(result);

        Common_Draw("%s %s", i == state->cursor ? ">" : " ", pluginname);
    }

    Common_Draw("");
    Common_Draw("==================================================");
    Common_Draw("");

    result = state->system->getPluginHandle(FMOD_PLUGINTYPE_DSP, state->cursor, &pluginhandle);
    ERRCHECK(result);

    const FMOD_DSP_DESCRIPTION *description;
    result = state->system->getDSPInfoByPlugin(pluginhandle, &description);
    ERRCHECK(result);

    drawDSPInfo(description);
}

void drawDSPParameters(ParameterViewerState *state)
{
    FMOD_RESULT              result;
    FMOD_DSP_PARAMETER_DESC *paramdesc;
    char                     pluginname[256];

    Common_Draw("Press %s to scroll down", Common_BtnStr(BTN_DOWN));
    Common_Draw("Press %s to scroll up", Common_BtnStr(BTN_UP));
    Common_Draw("Press %s to return to the plug-in list", Common_BtnStr(BTN_LEFT));
    Common_Draw("");
    
    result = state->dsp->getInfo(pluginname, 0, 0, 0, 0);
    ERRCHECK(result);

    Common_Draw("%s Parameters:", pluginname);
    Common_Draw("--------------------------------------------------");

    for (int i = state->scroll; i < state->numparams; i++)
    {
        result = state->dsp->getParameterInfo(i, &paramdesc);
        ERRCHECK(result);
        switch (paramdesc->type)
        {
            case FMOD_DSP_PARAMETER_TYPE_FLOAT:
            {
                char *units = paramdesc->label;
                Common_Draw("%2d: %-15s [%g, %g] (%.2f%s)", i, paramdesc->name, paramdesc->floatdesc.min, paramdesc->floatdesc.max, paramdesc->floatdesc.defaultval, units);
                break;
            }

            case FMOD_DSP_PARAMETER_TYPE_INT:
            {
                if (paramdesc->intdesc.valuenames)
                {
                    int lengthremaining = 1024;
                    char enums[1024];
                    char *s = enums;
                    for (int j = 0; j < paramdesc->intdesc.max - paramdesc->intdesc.min; ++j)
                    {
                        int len =  Common_snprintf(s, lengthremaining, "%s, ", paramdesc->intdesc.valuenames[j]);
                        if (!len)
                        {
                            break;
                        }
                        s += len;
                        lengthremaining -= len;
                    }
                    if (lengthremaining)
                    {
                        Common_snprintf(s, lengthremaining, "%s", paramdesc->intdesc.valuenames[paramdesc->intdesc.max - paramdesc->intdesc.min]);
                    }
                    Common_Draw("%2d: %-15s [%s] (%s)", i, paramdesc->name, enums, paramdesc->intdesc.valuenames[paramdesc->intdesc.defaultval - paramdesc->intdesc.min]);
                }
                else
                {
                    char *units = paramdesc->label;
                    Common_Draw("%2d: %-15s [%d, %d] (%d%s)", i, paramdesc->name, paramdesc->intdesc.min, paramdesc->intdesc.max, paramdesc->intdesc.defaultval, units);
                }
                break;
            }

            case FMOD_DSP_PARAMETER_TYPE_BOOL:
            {
                if (paramdesc->booldesc.valuenames)
                {
                    Common_Draw("%2d: %-15s [%s, %s] (%s)", i, paramdesc->name, paramdesc->booldesc.valuenames[0], paramdesc->booldesc.valuenames[1], paramdesc->booldesc.valuenames[paramdesc->booldesc.defaultval ? 1 : 0]);
                }
                else
                {
                    Common_Draw("%2d: %-15s [On, Off] (%s)", i, paramdesc->name, paramdesc->booldesc.defaultval ? "On" : "Off");
                }
                break;
            }

            case FMOD_DSP_PARAMETER_TYPE_DATA:
            {
                Common_Draw("%2d: %-15s (Data type: %d)", i, paramdesc->name, paramdesc->datadesc.datatype);
                break;
            }

            default:
                break;
        }
    }
}

InspectorState pluginSelectorDo(PluginSelectorState *state)
{
    if (Common_BtnPress(BTN_UP))
    {
        state->cursor = (state->cursor - 1 + state->numplugins) % state->numplugins;
    }

    if (Common_BtnPress(BTN_DOWN))
    {
        state->cursor = (state->cursor + 1) % state->numplugins;
    }

    if (Common_BtnPress(BTN_RIGHT))
    {
        return PARAMETER_VIEWER;
    }

    drawTitle();
    drawDSPList(state);

    return PLUGIN_SELECTOR;
}

InspectorState parameterViewerDo(ParameterViewerState *state)
{
    if (state->numparams > MAX_PARAMETERS_IN_VIEW)
    {
        if (Common_BtnPress(BTN_UP))
        {
            state->scroll--;
            state->scroll = Common_Max(state->scroll, 0);
        }

        if (Common_BtnPress(BTN_DOWN))
        {
            state->scroll++;
            state->scroll = Common_Min(state->scroll, state->numparams - MAX_PARAMETERS_IN_VIEW);
        }
    }

    if (Common_BtnPress(BTN_LEFT))
    {
        return PLUGIN_SELECTOR;
    }

    drawTitle();
    drawDSPParameters(state);

    return PARAMETER_VIEWER;
}

int FMOD_Main()
{
    FMOD::System        *system           = 0;
    FMOD_RESULT          result;
    void                *extradriverdata  = 0;
    unsigned int         pluginhandle;
    InspectorState       state            = PLUGIN_SELECTOR;
    PluginSelectorState  pluginselector   = { 0 };
    ParameterViewerState parameterviewer  = { 0 };

    Common_Init(&extradriverdata);

    /*
        Create a System object and initialize
    */
    result = FMOD::System_Create(&system);
    ERRCHECK(result);

    result = system->init(32, FMOD_INIT_NORMAL, extradriverdata);
    ERRCHECK(result);

    result = system->getNumPlugins(FMOD_PLUGINTYPE_DSP, &pluginselector.numplugins);
    ERRCHECK(result);

    pluginselector.system = system;

    do
    {
        Common_Update();

        if (state == PLUGIN_SELECTOR)
        {
            state = pluginSelectorDo(&pluginselector);

            if (state == PARAMETER_VIEWER)
            {
                result = pluginselector.system->getPluginHandle(FMOD_PLUGINTYPE_DSP, pluginselector.cursor, &pluginhandle);
                ERRCHECK(result);

                result = pluginselector.system->createDSPByPlugin(pluginhandle, &parameterviewer.dsp);
                ERRCHECK(result);

                FMOD_RESULT result = parameterviewer.dsp->getNumParameters(&parameterviewer.numparams);
                ERRCHECK(result);

                parameterviewer.scroll = 0;
            }
        }
        else if (state == PARAMETER_VIEWER)
        {
            state = parameterViewerDo(&parameterviewer);

            if (state == PLUGIN_SELECTOR)
            {
                result = parameterviewer.dsp->release();
                ERRCHECK(result);

                parameterviewer.dsp = 0;
            }
        }

        result = system->update();
        ERRCHECK(result);

        Common_Sleep(INTERFACE_UPDATETIME - 1);
    } while (!Common_BtnPress(BTN_QUIT));

    if (parameterviewer.dsp)
    {
        result = parameterviewer.dsp->release();
        ERRCHECK(result);
    }

    result = system->close();
    ERRCHECK(result);
    result = system->release();
    ERRCHECK(result);

    Common_Close();

    return 0;
}
