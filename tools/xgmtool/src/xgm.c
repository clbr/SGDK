#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>

#include "../inc/xgm.h"
#include "../inc/vgm.h"
#include "../inc/xgmtool.h"


// forward
static void XGM_parseMusic(XGM* xgm, unsigned char* data, int length);
static void XGM_extractSamples(XGM* xgm, VGM* vgm);
static void XGM_extractMusic(XGM* xgm, VGM* vgm);


XGM* XGM_create()
{
    XGM* result;

    result = malloc(sizeof(XGM));

    result->samples = NULL;
    result->commands = NULL;
    result->pal = -1;

    return result;
}

XGM* XGM_createFromData(unsigned char* data, int dataSize)
{
    int s;
    XGM* result = XGM_create();

    if (!silent)
        printf("Parsing XGM file...\n");

    if (strncasecmp(&data[0x00], "XGM ", 4))
    {
        printf("Error: XGM file not recognized !\n");
        return NULL;
    }

    // sample id table
    LList* samples = NULL;
    for (s = 1; s < 0x40; s++)
    {
        int offset = getInt16(data, (s * 4) + 0);
        int len = getInt16(data, (s * 4) + 2);

        // ignore empty sample
        if ((offset != 0xFFFF) && (len != 0x0100))
        {
            offset <<= 8;
            len <<= 8;

            // add sample
            samples = insertAfterLList(samples, XGMSample_create(s, data + (offset + 0x104), len, offset));
        }
    }

    result->samples = getHeadLList(samples);

    // calculate music data offset (sample block size + 0x104)
    int offset = (getInt16(data, 0x100) << 8) + 0x104;
    // int version = data[0x102];
    result->pal = data[0x103] & 1;

    // get music data length
    int len = getInt(data, offset);

    if (verbose)
    {
        printf("XGM sample number: %d\n", getSizeLList(result->samples));
        printf("XGM start music data: %6X  len: %d\n", offset + 4, len);
    }

    // build command list
    XGM_parseMusic(result, data + offset + 4, len);

    if (!silent)
        printf("XGM duration: %d frames (%d seconds)\n", XGM_computeLenInFrame(result), XGM_computeLenInSecond(result));

    return result;
}

XGM* XGM_createFromVGM(VGM* vgm)
{
    XGM* result = XGM_create();

    if (!silent)
        printf("Converting VGM to XGM...\n");

    if (vgm->rate == 60)
        result->pal = 0;
    else if (vgm->rate == 50)
        result->pal = 1;
    else
        result->pal = -1;

    // extract samples from VGM
    XGM_extractSamples(result, vgm);
    // and extract music data
    XGM_extractMusic(result, vgm);

    // display play PCM command
//    if (verbose)
//    {
//        LList* curCom = result->commands;
//        while(curCom != NULL)
//        {
//            XGMCommand* command = curCom->element;
//
//            if (XGMCommand_isPCM(command))
//                printf("play sample %2X at frame %d\n", XGMCommand_getPCMId(command), XGM_getTimeInFrame(result, command));
//
//            curCom = curCom->next;
//        }
//    }

    if (verbose)
    {
        printf("XGM sample number: %d\n", getSizeLList(result->samples));
        printf("Sample size: %d\n", XGM_getSampleDataSize(result));
        printf("Music data size: %d\n", XGM_getMusicDataSize(result));
    }
    if (!silent)
        printf("XGM duration: %d frames (%d seconds)\n", XGM_computeLenInFrame(result), XGM_computeLenInSecond(result));

    return result;
}


static void XGM_parseMusic(XGM* xgm, unsigned char* data, int length)
{
    // build command list
   int off;

    // parse all XGM commands
    off = 0;
    LList* commands = xgm->commands;
    while (off < length)
    {
        // check for loop start
        XGMCommand* command = XGMCommand_createFromData(data + off);
        commands = insertAfterLList(commands, command);
        off += command->size;

        // stop here
        if (XGMCommand_isEnd(command))
            break;
    }

    xgm->commands = getHeadLList(commands);

    if (!silent)
        printf("Number of command: %d\n", getSizeLList(xgm->commands));
}

static void XGM_extractSamples(XGM* xgm, VGM* vgm)
{
    int index;
    LList* sampleXgm;

    // index should be equal to current size + 1
    index = getSizeLList(xgm->samples) + 1;
    sampleXgm = getTailLList(xgm->samples);

    // extract samples
    LList* b = vgm->sampleBanks;
    while(b != NULL)
    {
        SampleBank* sampleBank = b->element;
        LList* s = sampleBank->samples;

        while(s != NULL)
        {
            XGMSample* sample = XGMSample_createFromVGMSample(sampleBank, s->element);

            // valid sample
            if (sample != NULL)
            {
                sample->index = index++;
                sampleXgm = insertAfterLList(sampleXgm, sample);
            }

            s = s->next;
        }

        b = b->next;
    }

    xgm->samples = getHeadLList(sampleXgm);
}

static void XGM_extractMusic(XGM* xgm, VGM* vgm)
{
    LList* frameCommands = NULL;
    LList* ymKeyOffCommands = NULL;
    LList* ymKeyOtherCommands = NULL;
    LList* ymPort0Commands = NULL;
    LList* ymPort1Commands = NULL;
    LList* psgCommands = NULL;
    LList* sampleCommands = NULL;

    LList* xgmCommands = NULL;

    int loopOffset = -1;
    bool lastCommWasKey;

    LList* vgmCom = vgm->commands;
    while (vgmCom != NULL)
    {
        // get frame commands
        deleteLList(frameCommands);
        frameCommands = NULL;

        while (vgmCom != NULL)
        {
            VGMCommand* command = vgmCom->element;
            vgmCom = vgmCom->next;

            if (VGMCommand_isLoop(command))
            {
                if (loopOffset == -1)
                    loopOffset = XGM_getMusicDataSizeOf(getHeadLList(xgm->commands));
                continue;
            }
            // ignore data block
            if (VGMCommand_isDataBlock(command))
                continue;
            // stop here
            if (VGMCommand_isWait(command))
            {
                // set PAL flag if not already set
                if (xgm->pal == -1)
                {
                    if (VGMCommand_isWaitPAL(command))
                        xgm->pal = 1;
                    else if (VGMCommand_isWaitNTSC(command))
                        xgm->pal = 0;
                }

                break;
            }

            if (VGMCommand_isEnd(command))
                break;

            // add command
            frameCommands = insertAfterLList(frameCommands, command);
        }

        // prepare new commands for this frame
        deleteLList(xgmCommands);
        xgmCommands = NULL;

        // group commands
        deleteLList(ymKeyOffCommands);
        deleteLList(ymKeyOtherCommands);
        deleteLList(ymPort0Commands);
        deleteLList(ymPort1Commands);
        deleteLList(psgCommands);
        deleteLList(sampleCommands);
        ymKeyOffCommands = NULL;
        ymKeyOtherCommands = NULL;
        ymPort0Commands = NULL;
        ymPort1Commands = NULL;
        psgCommands = NULL;
        sampleCommands = NULL;

        lastCommWasKey = false;

        LList* com = getHeadLList(frameCommands);
        while(com != NULL)
        {
            VGMCommand* command = com->element;

            if (VGMCommand_isStream(command))
                sampleCommands = insertAfterLList(sampleCommands, command);
            else if (VGMCommand_isPSGWrite(command))
                psgCommands = insertAfterLList(psgCommands, command);
            else if (VGMCommand_isYM2612KeyWrite(command))
            {
                if (VGMCommand_isYM2612KeyOffWrite(command))
                {
                    if (!VGMCommand_contains(getHeadLList(ymKeyOffCommands), command))
                        ymKeyOffCommands = insertAfterLList(ymKeyOffCommands, command);
                }
                else
                {
                    VGMCommand* previousCommand = VGMCommand_getKeyCommand(getHeadLList(ymKeyOtherCommands),
                            VGMCommand_getYM2612KeyChannel(command));

                    // change command with last one
                    if (previousCommand != NULL)
                        previousCommand->data[previousCommand->offset + 2] = command->data[command->offset + 2];
                    else
                        ymKeyOtherCommands = insertAfterLList(ymKeyOtherCommands, command);
                }

                lastCommWasKey = true;
            }
            else if (VGMCommand_isYM2612Write(command))
            {
                // need accurate key event so we transfer commands now
                if (lastCommWasKey)
                {
                    ymPort0Commands = getHeadLList(ymPort0Commands);
                    ymPort1Commands = getHeadLList(ymPort1Commands);
                    ymKeyOffCommands = getHeadLList(ymKeyOffCommands);
                    ymKeyOtherCommands = getHeadLList(ymKeyOtherCommands);
                    psgCommands = getHeadLList(psgCommands);
                    sampleCommands = getHeadLList(sampleCommands);

                    // general YM commands first as key event were just done
                    if (ymPort0Commands != NULL)
                        xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMPort0Commands(ymPort0Commands));
                    if (ymPort1Commands != NULL)
                        xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMPort1Commands(ymPort1Commands));
                    // then key off first
                    if (ymKeyOffCommands != NULL)
                        xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMKeyCommands(ymKeyOffCommands));
                    // followed by key on commands
                    if (ymKeyOtherCommands != NULL)
                        xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMKeyCommands(ymKeyOtherCommands));
                    // then PSG commands
                    if (psgCommands != NULL)
                        xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createPSGCommands(psgCommands));
                    // and finally PCM commands
                    if (sampleCommands != NULL)
                        xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createPCMCommands(xgm, vgm, sampleCommands));

                    deleteLList(ymKeyOffCommands);
                    deleteLList(ymKeyOtherCommands);
                    deleteLList(ymPort0Commands);
                    deleteLList(ymPort1Commands);
                    deleteLList(psgCommands);
                    deleteLList(sampleCommands);
                    ymKeyOffCommands = NULL;
                    ymKeyOtherCommands = NULL;
                    ymPort0Commands = NULL;
                    ymPort1Commands = NULL;
                    psgCommands = NULL;
                    sampleCommands = NULL;

                    lastCommWasKey = false;
                }

                if (VGMCommand_isYM2612Port0Write(command))
                    ymPort0Commands = insertAfterLList(ymPort0Commands, command);
                else
                    ymPort1Commands = insertAfterLList(ymPort1Commands, command);
            }
            else
            {
                if (verbose)
                    printf("Command %d ignored\n", command->command);
            }

            com = com->next;
        }

        ymPort0Commands = getHeadLList(ymPort0Commands);
        ymPort1Commands = getHeadLList(ymPort1Commands);
        ymKeyOffCommands = getHeadLList(ymKeyOffCommands);
        ymKeyOtherCommands = getHeadLList(ymKeyOtherCommands);
        psgCommands = getHeadLList(psgCommands);
        sampleCommands = getHeadLList(sampleCommands);

        // general YM commands first as key event were just done
        if (ymPort0Commands != NULL)
            xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMPort0Commands(ymPort0Commands));
        if (ymPort1Commands != NULL)
            xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMPort1Commands(ymPort1Commands));
        // then key off first
        if (ymKeyOffCommands != NULL)
            xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMKeyCommands(ymKeyOffCommands));
        // followed by key on commands
        if (ymKeyOtherCommands != NULL)
            xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createYMKeyCommands(ymKeyOtherCommands));
        // then PSG commands
        if (psgCommands != NULL)
            xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createPSGCommands(psgCommands));
        // and finally PCM commands
        if (sampleCommands != NULL)
            xgmCommands = insertAllAfterLList(xgmCommands, XGMCommand_createPCMCommands(xgm, vgm, sampleCommands));

        // last frame ?
        if (vgmCom == NULL)
        {
            // loop point ?
            if (loopOffset != -1)
                xgmCommands = insertAfterLList(xgmCommands, XGMCommand_createLoopCommand(loopOffset));
            else
                // add end command
                xgmCommands = insertAfterLList(xgmCommands, XGMCommand_createEndCommand());
        }

        // end frame
        xgmCommands = insertAfterLList(xgmCommands, XGMCommand_createFrameCommand());

        // finally add the new commands
        xgm->commands = insertAllAfterLList(xgm->commands, getHeadLList(xgmCommands));
    }

    // get back to head
    xgm->commands = getHeadLList(xgm->commands);

    // compute offset
    int offset = 0;
    LList* com = xgm->commands;
    while(com != NULL)
    {
        XGMCommand* command = com->element;

        XGMCommand_setOffset(command, offset);
        offset += command->size;

        com = com->next;
    }

    if (!silent)
        printf("Number of command: %d\n", getSizeLList(xgm->commands));
}

/**
 * Find the LOOP command
 */
XGMCommand* XGM_getLoopCommand(XGM* xgm)
{
    LList* com = xgm->commands;
    while(com != NULL)
    {
        XGMCommand* command = com->element;

        if (XGMCommand_isLoop(command))
            return command;

        com = com->next;
    }

    return NULL;
}

/**
 * Return the position of the command pointed by the loop
 */
LList* XGM_getLoopPointedCommandElement(XGM* xgm)
{
    XGMCommand* loopCommand = XGM_getLoopCommand(xgm);

    if (loopCommand != NULL)
        return XGM_getCommandElementAtOffset(xgm, XGMCommand_getLoopOffset(loopCommand));

    return NULL;
}

/**
 * Return the command pointed by the loop
 */
XGMCommand* XGM_getLoopPointedCommand(XGM* xgm)
{
    LList* com = XGM_getLoopPointedCommandElement(xgm);

    if (com != NULL)
        return com->element;

    return NULL;
}

int XGM_computeLenInFrame(XGM* xgm)
{
    int result = 0;
    LList* com = xgm->commands;

    while(com != NULL)
    {
        XGMCommand* command = com->element;

        if (XGMCommand_isFrame(command))
            result++;

        com = com->next;
    }

    return result;
}

int XGM_computeLenInSecond(XGM* xgm)
{
    return XGM_computeLenInFrame(xgm) / (xgm->pal ? 50 : 60);
}

/**
 * Return the offset of the specified command
 */
int XGM_getOffset(XGM* xgm, XGMCommand* command)
{
    int result = 0;
    LList* com = xgm->commands;

    while(com != NULL)
    {
        XGMCommand* c = com->element;

        if (c == command)
            return result;

        result += c->size;
        com = com->next;
    }

    return -1;
}

/**
 * Return elapsed time when specified command happen (in 1/44100 of second)
 */
int XGM_getTime(XGM* xgm, XGMCommand* command)
{
    int result = -1;
    LList* com = xgm->commands;

    while(com != NULL)
    {
        XGMCommand* c = com->element;

        if (XGMCommand_isFrame(c))
            result++;
        if (c == command)
            break;

        com = com->next;
    }

    // convert in sample (44100 Hz)
    return (result * 44100) / (xgm->pal ? 50 : 60);
}

/**
 * Return elapsed time when specified command happen (in frame)
 */
int XGM_getTimeInFrame(XGM* xgm, XGMCommand* command)
{
    return XGM_getTime(xgm, command) / (44100 / (xgm->pal ? 50 : 60));
}

LList* XGM_getCommandElementAtOffset(XGM* xgm, int offset)
{
    LList* com = xgm->commands;
    int curOffset = 0;

    while(com != NULL)
    {
        XGMCommand* command = com->element;

        if (curOffset == offset)
            return com;

        curOffset += command->size;
        com = com->next;
    }

    return NULL;
}

/**
 * Return elapsed time when specified command happen
 */
LList* XGM_getCommandElementAtTime(XGM* xgm, int time)
{
    int adjTime = (time * 60) / 44100;
    int result = 0;
    LList* com = xgm->commands;

    while(com != NULL)
    {
        XGMCommand* command = com->element;

        if (result >= adjTime)
            return com;
        if (XGMCommand_isFrame(command))
            result++;

        com = com->next;
    }

    return NULL;
}

XGMCommand* XGM_getCommandAtOffset(XGM* xgm, int offset)
{
    const LList* com = XGM_getCommandElementAtOffset(xgm, offset);

    if (com != NULL)
        return com->element;

    return NULL;
}

/**
 * Return command at specified time
 */
XGMCommand* XGM_getCommandAtTime(XGM* xgm, int time)
{
    const LList* com = XGM_getCommandElementAtTime(xgm, time);

    if (com != NULL)
        return com->element;

    return NULL;
}

XGMSample* XGM_getSampleByIndex(XGM* xgm, int index)
{
    if (index < 1) return NULL;

    const LList* sample = getElementAtLList(xgm->samples, index);

    if (sample != NULL)
        return sample->element;

    return NULL;
}

//XGMSample* XGM_getSampleByAddressAndLen(XGM* xgm, int addr, int len)
//{
//    int i;
//    int minLen;
//    int maxLen;
//
//    minLen = max(0, len - 50);
//    maxLen = len + 50;
//
//    for (i = 0; i < xgm->samples->size; i++)
//    {
//        XGMSample* sample = getFromList(xgm->samples, i);
//
//        if ((sample->originAddr == addr) && (sample->originSize >= minLen) && (sample->originSize <= maxLen))
//            return sample;
//    }
//
//    return NULL;
//}

XGMSample* XGM_getSampleByAddress(XGM* xgm, int originAddr)
{
    LList* l;

    l = xgm->samples;
    while(l != NULL)
    {
        XGMSample* sample = l->element;

        if (sample->originAddr == originAddr)
            return sample;

        l = l->next;
    }

    return NULL;
}

int XGM_getSampleDataSize(XGM* xgm)
{
    int result = 0;
    LList* l;

    l = xgm->samples;
    while(l != NULL)
    {
        XGMSample* sample = l->element;

        result += sample->dataSize;

        l = l->next;
    }

    return result;
}

int XGM_getMusicDataSizeOf(LList* commands)
{
    int result = 0;
    LList* c;

    c = commands;
    while(c != NULL)
    {
        XGMCommand* command = c->element;

        result += command->size;

        c = c->next;
    }

    return result;
}

int XGM_getMusicDataSize(XGM* xgm)
{
    return XGM_getMusicDataSizeOf(xgm->commands);
}

unsigned char* XGM_asByteArray(XGM* xgm, int *outSize)
{
    int i;
    int offset;
    unsigned char byte;
    FILE* f = fopen("tmp.bin", "wb+");
    LList* l;

    if (f == NULL)
    {
        printf("Error: cannot open file tmp.bin\n");
        return NULL;
    }

    // 0000: XGM (should be ignored in ROM resource)
    fwrite("XGM ", 1, 4, f);

    // 0004-0100: sample id table
    // fixed size : 252 bytes, limit music to 63 samples max
    offset = 0;
    i = 0;
    l = xgm->samples;
    while(l != NULL)
    {
        XGMSample* sample = l->element;
        const int len = sample->dataSize;

        byte = offset >> 8;
        fwrite(&byte, 1, 1, f);
        byte = offset >> 16;
        fwrite(&byte, 1, 1, f);
        byte = len >> 8;
        fwrite(&byte, 1, 1, f);
        byte = len >> 16;
        fwrite(&byte, 1, 1, f);
        offset += len;

        i++;
        l = l->next;
    }
    for (; i < 0x3F; i++)
    {
        // special mark for silent sample
        byte = 0xFF;
        fwrite(&byte, 1, 1, f);
        fwrite(&byte, 1, 1, f);
        byte = 0x00;
        fwrite(&byte, 1, 1, f);
        fwrite(&byte, 1, 1, f);
    }

    // 0100-0101: sample block size *256 (2 bytes)
    byte = offset >> 8;
    fwrite(&byte, 1, 1, f);
    byte = offset >> 16;
    fwrite(&byte, 1, 1, f);

    // init PAL flag if needed (default is NTSC)
    if (xgm->pal == -1)
        xgm->pal = 0;

    // 0102: XGM version
    byte = 0x00;
    fwrite(&byte, 1, 1, f);
    // 0103: b0=NTSC/PAL others=reserved
    byte = xgm->pal & 1;
    fwrite(&byte, 1, 1, f);

    // 0104-XXXX: sample data
    l = xgm->samples;
    while(l != NULL)
    {
        XGMSample* sample = l->element;
        fwrite(sample->data, 1, sample->dataSize, f);
        l = l->next;
    }

    // compute XGM music data size in byte
    const int len = XGM_getMusicDataSize(xgm);

    // XXXX+0000: music data size (in byte)
    byte = len >> 0;
    fwrite(&byte, 1, 1, f);
    byte = len >> 8;
    fwrite(&byte, 1, 1, f);
    byte = len >> 16;
    fwrite(&byte, 1, 1, f);
    byte = len >> 24;
    fwrite(&byte, 1, 1, f);

    // XXXX+0004: music data
    l = xgm->commands;
    while(l != NULL)
    {
        XGMCommand* command = l->element;
        fwrite(command->data, 1, command->size, f);
        l = l->next;
    }

    unsigned char* result = inEx(f, 0, getFileSizeEx(f), outSize);

    fclose(f);

    return result;
}