/*
 * Copyright (c) from 2000 to 2009
 * 
 * Network and System Laboratory 
 * Department of Computer Science 
 * College of Computer Science
 * National Chiao Tung University, Taiwan
 * All Rights Reserved.
 * 
 * This source code file is part of the NCTUns 6.0 network simulator.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation is hereby granted (excluding for commercial or
 * for-profit use), provided that both the copyright notice and this
 * permission notice appear in all copies of the software, derivative
 * works, or modified versions, and any portions thereof, and that
 * both notices appear in supporting documentation, and that credit
 * is given to National Chiao Tung University, Taiwan in all publications 
 * reporting on direct or indirect use of this code or its derivatives.
 *
 * National Chiao Tung University, Taiwan makes no representations 
 * about the suitability of this software for any purpose. It is provided 
 * "AS IS" without express or implied warranty.
 *
 * A Web site containing the latest NCTUns 6.0 network simulator software 
 * and its documentations is set up at http://NSL.csie.nctu.edu.tw/nctuns.html.
 *
 * Project Chief-Technology-Officer
 * 
 * Prof. Shie-Yuan Wang <shieyuan@csie.nctu.edu.tw>
 * National Chiao Tung University, Taiwan
 *
 * 09/01/2009
 */

#include "ofdma_pmpbs_NT.h"

MODULE_GENERATOR(OFDMA_PMPBS_NT);

//extern SLIST_HEAD(wimax_head, con_list) headOfMobileWIMAX_;
extern SLIST_HEAD(wimax_head, con_list) headOfMRWIMAXNT_;
extern typeTable *typeTable_;
extern char *MR_WIMAXChannelCoding_NT;

    OFDMA_PMPBS_NT::OFDMA_PMPBS_NT(uint32_t type, uint32_t id, struct plist *pl, const char *name)
:OFDMA_80216j_NT(type, id, pl, name)
{
    /* Initial Registers */
    recvState           = Busy_access;
    //_Gain               = 15;
}

OFDMA_PMPBS_NT::~OFDMA_PMPBS_NT()
{
    delete timerIdle;
}

int OFDMA_PMPBS_NT::init(void)
{
    _DCDProfile = GET_REG_VAR(get_port(), "DCDProfile", struct DCDBurstProfile *);
    _UCDProfile = GET_REG_VAR(get_port(), "UCDProfile", struct UCDBurstProfile *);

    timerIdle = new timerObj;

    BASE_OBJTYPE(mem_func);

    mem_func = POINTER_TO_MEMBER(OFDMA_PMPBS_NT, setStateIdle);
    timerIdle->setCallOutObj(this, mem_func);

    return NslObject::init();
}

int OFDMA_PMPBS_NT::recv(ePacket_ *epkt)
{
    Packet *pkt             = NULL;
    double recvPower        = 0.0;
    double *dist            = NULL;
    struct PHYInfo *phyInfo = NULL;
    struct LogInfo *log_info= NULL;
    uint64_t timeInTick     = 0;

    struct wphyInfo *wphyinfo;

    /* Get packet information */
    pkt     = (Packet *) epkt->DataInfo_;
    phyInfo = (struct PHYInfo *) pkt->pkt_getinfo("phyInfo");
    dist    = (double *) pkt->pkt_getinfo("dist");
    wphyinfo= (struct wphyInfo *)pkt->pkt_getinfo("WPHY");

    assert(pkt && phyInfo && dist && wphyinfo);

    /******** LOG START ********/
    int log_flag = (!strncasecmp(MR_WIMAX_NT_LogFlag, "on", 2)) ? 1 : 0;

    if (log_flag)
    {
        uint64_t start_recv_time = GetCurrentTime();
        log_info = (struct LogInfo *) pkt->pkt_getinfo("loginfo");

        if (!log_info)
        {
            printf("OFDMA_PMPBS_NT::recv(): missing log information.\n");
            exit(1);
        }

        pkt->pkt_addinfo("srecv_t", (char *) &start_recv_time, sizeof(uint64_t));
    }
    /******** LOG END *******/

    /* Check channel ID */
    if (phyInfo->ChannelID != _ChannelID) 
    {
        freePacket(epkt);
        return 1;
    }

    /* Compute recvPower and SNR */
    recvPower       = wphyinfo->Pr_;
    phyInfo->SNR    = _channelModel->powerToSNR(recvPower);

    /* Check receive power and compare with sensitivity */
    if (recvPower < _RecvSensitivity)
    {
        freePacket(epkt);
        return 1;
    }

    /* Compute transmission time */
    if (phyInfo->uiuc == CDMA_BWreq_Ranging) // Ranging Code
    {
        if (phyInfo->ulmap_ie.ie_12.rangMethod == 0)        // Initial or Handover Ranging over 2 symbols
        {
            MICRO_TO_TICK(timeInTick, 2 * Ts);
        }
        else if (phyInfo->ulmap_ie.ie_12.rangMethod == 1)   // Initial or Handover Ranging over 4 symbols
        {
            MICRO_TO_TICK(timeInTick, 4 * Ts);
        }
        else if (phyInfo->ulmap_ie.ie_12.rangMethod == 2)   // BW Request or Period Ranging over 1 symbol
        {
            MICRO_TO_TICK(timeInTick, 1 * Ts);
        }
        else                                                // BW Request or Period Ranging over 3 symbols
        {
            MICRO_TO_TICK(timeInTick, 3 * Ts);
        }
    }
    else if (phyInfo->uiuc == CDMA_Alloc_IE) // CDMA Allocation
    {
        int Nsymbols = 0;

        if(phyInfo->relay_flag == true)
        {
            if (phyInfo->ulmap_ie.ie_14.duration * UL_PUSC > _ULRelaySymbols)
                Nsymbols = _ULRelaySymbols;
            else
                Nsymbols = phyInfo->ulmap_ie.ie_14.duration * UL_PUSC;
        }  
        else
        {
            if (phyInfo->ulmap_ie.ie_14.duration * UL_PUSC > _ULAccessSymbols)
                Nsymbols = _ULAccessSymbols;
            else
                Nsymbols = phyInfo->ulmap_ie.ie_14.duration * UL_PUSC;
        }
        MICRO_TO_TICK(timeInTick, Nsymbols * Ts);
    }
    else // Normal burst
    {
        /* Here we assume that the delay time is the duration of UL subframe. */
        if(phyInfo->relay_flag == true)
            MICRO_TO_TICK(timeInTick, _ULRelaySymbols * Ts);
        else
            MICRO_TO_TICK(timeInTick, _ULAccessSymbols * Ts);
    }

    timeInTick += GetCurrentTime();
    BASE_OBJTYPE(type);
    type = POINTER_TO_MEMBER(OFDMA_PMPBS_NT, recvHandler);
    setObjEvent(epkt, timeInTick, 0, this, type, epkt->DataInfo_);

    return 1;
}

int OFDMA_PMPBS_NT::send(ePacket_ *epkt)
{
    BurstInfo *burstInfo    = NULL;
    downBurst *burst        = NULL;
    Packet *dlFrame_pkt     = NULL;
    Packet *dlrelayFrame_pkt     = NULL;
    char *input             = NULL;
    char *output            = NULL;
    char *tmp_dlfp          = NULL;
    int numBytes            = 0;
    int slots               = 0;
    int repe                = 0;
    int fec                 = 0;
    int len                 = 0;
    int n                   = 0;
    bool    RELAY_BURST     = false;
    struct DLFP_except_128 dl_frame_prefix;
    struct RzonePrefix_except_128 rzone_prefix;
    struct PHYInfo phyInfo;
    vector<downBurst *> *DLAccessBurstCollection = NULL;
    vector<downBurst *> *DLRelayBurstCollection = NULL;
    vector<downBurst *>::iterator itd;

    /* Get the burst information */
    burstInfo       = (BurstInfo *) epkt->DataInfo_;

    if(burstInfo->type == DL_RELAY)
    {
        //printf("\e[35mTime:%llu:OFDMA_PMPBS_NT(%d)::%s recv DLRelayBurstCollection\e[0m\n", GetCurrentTime(),get_nid(),__func__);
        RELAY_BURST = true;
        DLRelayBurstCollection = (vector <downBurst *> *)burstInfo->Collection;
        if(DLRelayBurstCollection->empty())
        {
            fprintf(stderr,"\e[35mTime:%llu:OFDMA_PMPRS_NT(%d)::%s DLRelayBurstCollection is empty!!!\e[0m\n",GetCurrentTime(),get_nid(),__func__);
            exit(1);
        }

        /* 1. Compute total length */
        // Get coded length of RzonePrefix (12 Byte * 4 repetition)
        repe        = REPETATION_CODEWORD_4;
        fec         = QPSK_1_2;
        numBytes    = _channelCoding->getCodedSlotLen(fec) * repeat_times_NT[repe];

        // Get coded length of R-MAP 
        repe        = REPETATION_CODEWORD_1;
        fec         = QPSK_1_2;
        numBytes    += _channelCoding->getCodedBurstLen(_rmapLen, repeat_times_NT[repe], fec);

        // Get coded length of DL relay Bursts
        for (itd = DLRelayBurstCollection->begin();itd != DLRelayBurstCollection->end();itd++)
        {
            burst       = *itd;
            fec         = _DCDProfile[burst->ie->_diuc].fec;
            slots       = (burst->dlmap_ie.ie_other.numSym * burst->dlmap_ie.ie_other.numCh / 2);
            len         = _channelCoding->getUncodedSlotLen(fec) * slots;
            n           = _channelCoding->getCodedBurstLen(len, repeat_times_NT[repe], fec);
            numBytes    += n;
        }

        /* 2. Allocate a new Packet, packet length depends on the frame length. */
        dlrelayFrame_pkt = new Packet;
        assert(dlrelayFrame_pkt);
        output      = dlrelayFrame_pkt->pkt_sattach(numBytes);
        dlrelayFrame_pkt->pkt_sprepend(output, numBytes);
        dlrelayFrame_pkt->pkt_setflow(PF_RECV);

        /* 3. Convey additional PHY information. */
        memset(&phyInfo, 0, sizeof(struct PHYInfo));
        phyInfo.power       = _transPower;
        phyInfo.ChannelID   = _ChannelID;
        phyInfo.ULAccessSym = _ULAccessSymbols;
        phyInfo.ULRelaySym  = _ULRelaySymbols;
        phyInfo.DLAccessSym = _DLAccessSymbols;
        phyInfo.DLRelaySym  = _DLRelaySymbols;
        phyInfo.relay_flag  = false;
        phyInfo.burst_type  = DL_RELAY;
        phyInfo.nid         = get_nid();
        phyInfo.pid         = get_port();
#if NT_DEBUG
        printf("----- OFDMA_PMPBS_NT[%d]::%s() burst_type=%d -----\n",get_nid(),__func__,phyInfo.burst_type);
        printf("phyInfo.ULAccessSym = %d\n",phyInfo.ULAccessSym);
        printf("phyInfo.ULRelaySym = %d\n",phyInfo.ULRelaySym);
        printf("phyInfo.DLAccessSym = %d\n",phyInfo.DLAccessSym);
        printf("phyInfo.DLRelaySym = %d\n",phyInfo.DLRelaySym);
        printf("---------------------------------\n\n");
#endif
        dlrelayFrame_pkt->pkt_addinfo("phyInfo", (char *) &phyInfo, sizeof(struct PHYInfo));

        /* 4. Build and encode the DLFP message */
        buildRzonePrefix(&rzone_prefix, _rmapLen);

        // Duplicate 2 times to form a FEC block and repeat 4 times
        repe        = REPETATION_CODEWORD_4;
        fec         = QPSK_1_2;
        len         = sizeof(struct RzonePrefix_except_128);  // 4 
        tmp_dlfp    = new char [len * 2];
        memcpy(tmp_dlfp, &rzone_prefix, len);
        memcpy(tmp_dlfp + len, &rzone_prefix, len);
        slots       = _channelCoding->getNumUncodedSlotWithRepe(len * 2, repeat_times_NT[repe], fec);

        for (int i = 0;i < repeat_times_NT[repe];i++)
        {
            n = _channelCoding->encode(tmp_dlfp, output + i * 12, len * 2, slots, repeat_times_NT[repe], fec);
        }
        output += n;
        delete [] tmp_dlfp;

        /* 5. Encode the R-MAP message */
        repe    = REPETATION_CODEWORD_1;
        fec     = QPSK_1_2;
        slots   = _channelCoding->getNumUncodedSlotWithRepe(_rmapLen, repeat_times_NT[repe], fec);
        n       = _channelCoding->encode(_rmapBuf, output, _rmapLen, slots, repeat_times_NT[repe], fec);
        output += n;
        delete _rmapBuf;

        /* 6. Encode the DL bursts */
        for (itd = DLRelayBurstCollection->begin();itd != DLRelayBurstCollection->end();itd++)
        {
            burst   = *itd;
            repe    = REPETATION_CODEWORD_1;
            fec     = _DCDProfile[burst->ie->_diuc].fec;
            len     = burst->length;
            input   = burst->payload;
            slots   = burst->dlmap_ie.ie_other.numSym * burst->dlmap_ie.ie_other.numCh / DL_PUSC;
            n       = _channelCoding->encode(input, output, len, slots, repeat_times_NT[repe], fec);
            output += n;
#if     0 
            if(burst->ie)
            {
                int diuc = burst->ie->_diuc;
                if(diuc!=Extended_DIUC_2 && diuc != Extended_DIUC && diuc != 13 && burst->nCid!=0)
                {
                    delete [] burst->dlmap_ie.ie_other.cid;
                }
                delete burst->ie;
                burst->ie = NULL;
            }
            delete [] burst->payload;
            burst->payload = NULL;
#endif

        }

        //printf("\e[35mTime:%llu:OFDMA_PMPBS_NT(%d)::%s() DLRelayBurst\e[0m\n", GetCurrentTime(),get_nid(),__func__);
        /********** LOG START **********/
        int log_flag = (!strncasecmp(MR_WIMAX_NT_LogFlag, "on", 2)) ? 1 : 0;

        if (log_flag)
        {
            struct LogInfo log_info;
            struct logEvent *logep      = NULL;
            uint8_t burst_type          = FRAMETYPE_MR_WIMAX_NT_PMP_DLRelayBURST;
            double  trans_time          = _DLRelaySymbols * Ts;
            uint64_t trans_time_in_tick = 0;

            //printf("transTime:%llu PMPBS::send() burst_len=%d,src=%d,dst=%d Txdone=%llu\n",GetCurrentTime(),numBytes,get_nid(),PHY_BROADCAST_ID,GetCurrentTime()+trans_time_in_tick);

            MICRO_TO_TICK(trans_time_in_tick, trans_time);
            memset(&log_info, 0, sizeof(struct LogInfo));

            log_info.burst_len  = numBytes;
            log_info.channel_id = _ChannelID;
            log_info.src_nid    = get_nid();
            log_info.dst_nid    = PHY_BROADCAST_ID;

            /* log StartTX event */
            mac80216j_NT_log_t *mac80216j_NT_log_p1 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

            LOG_MAC_802_16j_NT(mac80216j_NT_log_p1, GetCurrentTime(), GetCurrentTime(), get_type(), get_nid(), StartTX,
                    log_info.src_nid, log_info.dst_nid, burst_type, log_info.burst_len, log_info.channel_id);

            INSERT_TO_HEAP(logep, mac80216j_NT_log_p1->PROTO, mac80216j_NT_log_p1->Time + START, mac80216j_NT_log_p1);
#if LOG_DEBUG
            printf("BS: %llu ~ %llu %d->%d (StartTX)\n", GetCurrentTime(), GetCurrentTime(), log_info.src_nid, log_info.dst_nid);
#endif
            /* log SuccessTX event */
            mac80216j_NT_log_t *mac80216j_NT_log_p2 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

            LOG_MAC_802_16j_NT(mac80216j_NT_log_p2, GetCurrentTime() + trans_time_in_tick, GetCurrentTime(), get_type(), get_nid(), SuccessTX,
                    log_info.src_nid, log_info.dst_nid, burst_type, log_info.burst_len, log_info.channel_id);

            INSERT_TO_HEAP(logep, mac80216j_NT_log_p2->PROTO, mac80216j_NT_log_p2->Time + ENDING, mac80216j_NT_log_p2);
#if LOG_DEBUG
            printf("BS: %llu ~ %llu %d->%d (SuccessTX)\n", GetCurrentTime(), GetCurrentTime() + trans_time_in_tick, log_info.src_nid, log_info.dst_nid);
#endif
            dlrelayFrame_pkt->pkt_addinfo("loginfo", (char *) &log_info, sizeof(struct LogInfo));
        }
        /******** LOG END *********/

        /* 7. Send the DL packet to SubStations. */
        sendToSubStations(dlrelayFrame_pkt); 
        delete dlrelayFrame_pkt;
    }
    else
    {
        DLAccessBurstCollection = (vector<downBurst *> *) burstInfo->Collection;

        /* Check the DLAccessBurstCollection size */
        if (DLAccessBurstCollection->size() == 0)
        {
            fprintf(stderr, "\e[1;31mTime:%llu:OFDMA_PMPBS_NT(%d): DLAccessBurstCollection is Empty!\e[0m\n", GetCurrentTime(), get_nid());
            exit(1);
        }
        //printf("1.numBytes = %d\n",numBytes);

        /* 1. Compute total length */
        // Get coded length of DLFP message (12 Byte * 4 repetition)
        repe        = REPETATION_CODEWORD_4;
        fec         = QPSK_1_2;
        numBytes    = _channelCoding->getCodedSlotLen(fec) * repeat_times_NT[repe];
        //printf("2.numBytes = %d\n",numBytes);

        // Get coded length of DL-MAP message
        repe        = REPETATION_CODEWORD_1;
        fec         = QPSK_1_2;
        numBytes    += _channelCoding->getCodedBurstLen(_dlmapLen, repeat_times_NT[repe], fec);
        //printf("3.numBytes = %d\n",numBytes);

        // Get coded length of downBursts
        for (itd = DLAccessBurstCollection->begin();itd != DLAccessBurstCollection->end();itd++)
        {
            burst       = *itd;
            fec         = _DCDProfile[burst->ie->_diuc].fec;
            slots       = (burst->dlmap_ie.ie_other.numSym * burst->dlmap_ie.ie_other.numCh / 2);
            len         = _channelCoding->getUncodedSlotLen(fec) * slots;
            n           = _channelCoding->getCodedBurstLen(len, repeat_times_NT[repe], fec);
            numBytes    += n;
        }

        /* 2. Allocate a new Packet, packet length depends on the frame length. */
        dlFrame_pkt = new Packet;
        assert(dlFrame_pkt);
        //printf("5.numBytes = %d\n",numBytes);
        output      = dlFrame_pkt->pkt_sattach(numBytes);
        dlFrame_pkt->pkt_sprepend(output, numBytes);
        dlFrame_pkt->pkt_setflow(PF_RECV);

        /* 3. Convey additional PHY information. */
        memset(&phyInfo, 0, sizeof(struct PHYInfo));
        phyInfo.power       = _transPower;
        phyInfo.ChannelID   = _ChannelID;
        phyInfo.ULAccessSym = _ULAccessSymbols;
        phyInfo.ULRelaySym  = _ULRelaySymbols;
        phyInfo.DLAccessSym = _DLAccessSymbols;
        phyInfo.DLRelaySym  = _DLRelaySymbols;
        phyInfo.relay_flag  = false;
        phyInfo.burst_type  = DL_ACCESS;
        phyInfo.nid         = get_nid();
        phyInfo.pid         = get_port();

#if NT_DEBUG  
        printf("----- OFDMA_PMPBS_NT[%d]::%s() burst_type=%d -----\n",get_nid(),__func__,phyInfo.burst_type);
        printf("phyInfo.ULAccessSym = %d\n",phyInfo.ULAccessSym);
        printf("phyInfo.ULRelaySym = %d\n",phyInfo.ULRelaySym);
        printf("phyInfo.DLAccessSym = %d\n",phyInfo.DLAccessSym);
        printf("phyInfo.DLRelaySym = %d\n",phyInfo.DLRelaySym);
        printf("---------------------------------\n\n");
#endif
        dlFrame_pkt->pkt_addinfo("phyInfo", (char *) &phyInfo, sizeof(struct PHYInfo));

        /* 4. Build and encode the DLFP message */
        buildDLFP(&dl_frame_prefix, _dlmapLen);

        // Duplicate 2 times to form a FEC block and repeat 4 times
        repe        = REPETATION_CODEWORD_4;
        fec         = QPSK_1_2;
        len         = sizeof(struct DLFP_except_128);  // 3
        tmp_dlfp    = new char [len * 2];
        memcpy(tmp_dlfp, &dl_frame_prefix, len);
        memcpy(tmp_dlfp + len, &dl_frame_prefix, len);
        slots       = _channelCoding->getNumUncodedSlotWithRepe(len * 2, repeat_times_NT[repe], fec);

        for (int i = 0;i < repeat_times_NT[repe];i++)
        {
            n = _channelCoding->encode(tmp_dlfp, output + i * 12, len * 2, slots, repeat_times_NT[repe], fec);
        }
        output += n;
        delete [] tmp_dlfp;

        /* 5. Encode the DL-MAP message */
        repe    = REPETATION_CODEWORD_1;
        fec     = QPSK_1_2;
        slots   = _channelCoding->getNumUncodedSlotWithRepe(_dlmapLen, repeat_times_NT[repe], fec);
        n       = _channelCoding->encode(_dlmapBuf, output, _dlmapLen, slots, repeat_times_NT[repe], fec);
        output += n;
        delete _dlmapBuf;

        /* 6. Encode the DL bursts */
        for (itd = DLAccessBurstCollection->begin();itd != DLAccessBurstCollection->end();itd++)
        {
            burst   = *itd;
            repe    = REPETATION_CODEWORD_1;
            fec     = _DCDProfile[burst->ie->_diuc].fec;
            len     = burst->length;
            input   = burst->payload;
            slots   = burst->dlmap_ie.ie_other.numSym * burst->dlmap_ie.ie_other.numCh / DL_PUSC;
            n       = _channelCoding->encode(input, output, len, slots, repeat_times_NT[repe], fec);
            output += n;
#if     0 
            if(burst->ie)
            {
                int diuc = burst->ie->_diuc;
                if(diuc!=Extended_DIUC_2 && diuc != Extended_DIUC && diuc != 13 && burst->nCid!=0)
                {
                    delete [] burst->dlmap_ie.ie_other.cid;
                }
                delete burst->ie;
                burst->ie = NULL;
            }
            delete [] burst->payload;
            burst->payload = NULL;
#endif

        }
        //printf("\n\e[35mTime:%llu:OFDMA_PMPBS_NT(%d)::%s() DLAccessBurst\e[0m\n", GetCurrentTime(),get_nid(),__func__);
        /********** LOG START **********/
        int log_flag = (!strncasecmp(MR_WIMAX_NT_LogFlag, "on", 2)) ? 1 : 0;

        if (log_flag)
        {
            struct LogInfo log_info;
            struct logEvent *logep      = NULL;
            uint8_t     burst_type           = FRAMETYPE_MR_WIMAX_NT_PMP_DLAccessBURST;
            double      trans_time           = _DLAccessSymbols * Ts;
            uint64_t trans_time_in_tick = 0;

            //printf("transTime:%llu PMPBS::send() burst_len=%d,src=%d,dst=%d Txdone=%llu\n",GetCurrentTime(),numBytes,get_nid(),PHY_BROADCAST_ID,GetCurrentTime()+trans_time_in_tick);

            MICRO_TO_TICK(trans_time_in_tick, trans_time);
            memset(&log_info, 0, sizeof(struct LogInfo));

            log_info.burst_len  = numBytes;
            log_info.channel_id = _ChannelID;
            log_info.src_nid    = get_nid();
            log_info.dst_nid    = PHY_BROADCAST_ID;

            /* log StartTX event */
            mac80216j_NT_log_t *mac80216j_NT_log_p1 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

            LOG_MAC_802_16j_NT(mac80216j_NT_log_p1, GetCurrentTime(), GetCurrentTime(), get_type(), get_nid(), StartTX,
                    log_info.src_nid, log_info.dst_nid, burst_type, log_info.burst_len, log_info.channel_id);

            INSERT_TO_HEAP(logep, mac80216j_NT_log_p1->PROTO, mac80216j_NT_log_p1->Time + START, mac80216j_NT_log_p1);
#if LOG_DEBUG
            printf("BS: %llu ~ %llu %d->%d (StartTX)\n", GetCurrentTime(), GetCurrentTime(), log_info.src_nid, log_info.dst_nid);
#endif
            /* log SuccessTX event */
            mac80216j_NT_log_t *mac80216j_NT_log_p2 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

            LOG_MAC_802_16j_NT(mac80216j_NT_log_p2, GetCurrentTime() + trans_time_in_tick, GetCurrentTime(), get_type(), get_nid(), SuccessTX,
                    log_info.src_nid, log_info.dst_nid, burst_type, log_info.burst_len, log_info.channel_id);

            INSERT_TO_HEAP(logep, mac80216j_NT_log_p2->PROTO, mac80216j_NT_log_p2->Time + ENDING, mac80216j_NT_log_p2);
#if LOG_DEBUG
            printf("BS: %llu ~ %llu %d->%d (SuccessTX)\n", GetCurrentTime(), GetCurrentTime() + trans_time_in_tick, log_info.src_nid, log_info.dst_nid);
#endif
            dlFrame_pkt->pkt_addinfo("loginfo", (char *) &log_info, sizeof(struct LogInfo));
        }
        /******** LOG END *********/

        /* 7. Send the DL packet to SubStations. */
        sendToSubStations(dlFrame_pkt);
        delete dlFrame_pkt;
    } 

    /* Set epkt->DataInfo_ to NULL so that freePacket() won't free it again. */
#ifdef nt 
    if(burstInfo->Collection != NULL)
    {
        downBurst *burst = NULL;
        while(burstInfo->Collection->size())
        {
            burst = (downBurst *)burstInfo->Collection->back();
            burst->Clear_downBurst();
            delete burst;
            burst = NULL;
            burstInfo->Collection->pop_back();
        }

        delete burstInfo->Collection; 
        burstInfo->Collection = NULL;
    }
#endif
    epkt->DataInfo_ = NULL;

    //printf("burstInfo : type=%d size=%d\n",burstInfo->type, burstInfo->Collection->size());

    //printf("delete burstInfo : %x\n",burstInfo);
    delete burstInfo;

    /* If the upper module wants to temporarily hold outgoing packets for    */
    /* retransmission, it is the upper module's responsibility to duplicate  */
    /* another copy of this packet.                                          */
    freePacket(epkt);
    return 1;
}

void OFDMA_PMPBS_NT::sendToSubStations(Packet *dlFrame_pkt)
{
    ePacket_ *epkt              = NULL;
    struct con_list *wi         = NULL;
    const char *nodeType        = NULL;
    double dist                 = 0.0;
    struct wphyInfo *wphyinfo   = NULL;
    double currAzimuthAngle     = 0.0;
    double T_locX               = 0.0;
    double T_locY               = 0.0;
    double T_locZ               = 0.0;

    //printf("\e[35mTime:%llu:OFDMA_PMPBS_NT(%d)::%s()\e[0m\n", GetCurrentTime(),get_nid(),__func__);
    wphyinfo = (struct wphyInfo *) malloc(sizeof(struct wphyInfo));
    assert(wphyinfo);

    GetNodeLoc(get_nid(), T_locX, T_locY, T_locZ);
    currAzimuthAngle = getAntennaDirection(pointingDirection, angularSpeed);

    wphyinfo->nid               = get_nid();
    wphyinfo->pid               = get_port();
    wphyinfo->bw_               = 0;
    wphyinfo->channel_          = _ChannelID;
    wphyinfo->TxPr_             = pow(10, _transPower/10) * 1e-3; // Watt
    wphyinfo->RxPr_             = 0.0;
    wphyinfo->srcX_             = T_locX;
    wphyinfo->srcY_             = T_locY;
    wphyinfo->srcZ_             = T_locZ;
    wphyinfo->currAzimuthAngle_ = currAzimuthAngle;
    wphyinfo->Pr_               = 0.0;

    dlFrame_pkt->pkt_addinfo("WPHY", (char *)wphyinfo, sizeof(struct wphyInfo));
    free(wphyinfo);

    dlFrame_pkt->pkt_setflow(PF_SEND);

    //SLIST_FOREACH(wi, &headOfMobileWIMAX_, nextLoc)
    SLIST_FOREACH(wi, &headOfMRWIMAXNT_, nextLoc)
    {
        nodeType = typeTable_->toName(wi->obj->get_type());

        /* The receiver should not be myself */
        if (get_nid() == wi->obj->get_nid() && get_port() == wi->obj->get_port())
            continue;

        /* We only send the frame to PMPMS & PMPRS module */
        if (strcmp(nodeType, "MR_WIMAX_NT_PMPMS") != 0 && strcmp(nodeType, "MR_WIMAX_NT_PMPRS") != 0)
            continue;

        epkt = createEvent();
        epkt->DataInfo_ = dlFrame_pkt->copy();

        ((Packet *) epkt->DataInfo_)->pkt_addinfo("dist", (char *) &dist, sizeof(double));

        epkt->timeStamp_    = 0;
        epkt->perio_        = 0;
        epkt->calloutObj_   = wi->obj;
        epkt->memfun_       = NULL;
        epkt->func_         = NULL;
        NslObject::send(epkt);
    }
}

void OFDMA_PMPBS_NT::recvHandler(ePacket_ *epkt)
{
    ePacket_ *deliver_epkt      = NULL;
    Packet *pkt                 = NULL;
    struct PHYInfo *phyInfo     = NULL;
    struct LogInfo *log_info    = NULL;
    struct wphyInfo *wphyinfo   = NULL;
    double *dist                = NULL;
    char *input                 = NULL;
    char *output                = NULL;
    int slots                   = 0;
    int fec                     = 0;
    int repe                    = 0;
    int uiuc                    = 0;
    int codedLen                = 0;
    int uncodedLen              = 0;
    double avgSNR               = 0;
    double SNR                  = 0;
    double BER                  = 0;

    int log_flag = (!strncasecmp(MR_WIMAX_NT_LogFlag, "on", 2)) ? 1 : 0;
    /* Get the received packet information */
    pkt     = (Packet *) epkt->DataInfo_;
    wphyinfo= (struct wphyInfo *) pkt->pkt_getinfo("WPHY");
    input   = pkt->pkt_sget();
    phyInfo = (struct PHYInfo *) pkt->pkt_getinfo("phyInfo");
    dist    = (double *) pkt->pkt_getinfo("dist");
    uiuc    = phyInfo->uiuc;

    /* Unsafe state transformation */
    if (recvState == Busy_access || recvState == Busy_relay)
    {
        //printf("\n\e[1;35mTime:%llu BS(%d) PHY Busy (recvState = %d) drop packet !!!\e[0m\n",GetCurrentTime(),get_nid(),recvState);
        /********* LOG START *********/
        if (log_flag)
        {
            log_info = (struct LogInfo *) pkt->pkt_getinfo("loginfo");

            if (!log_info)
            {
                fprintf(stderr, "OFDMA_PMPBS_NT::recvHandler(): missing log information.\n");
                exit(1);
            }

            struct logEvent *logep      = NULL;
            uint8_t burst_type          = FRAMETYPE_MR_WIMAX_NT_PMP_ULAccessBURST;
            uint64_t *start_recv_time   = (uint64_t *)pkt->pkt_getinfo("srecv_t");

            /* log StartRX packets */
            mac80216j_NT_log_t *mac80216j_NT_log_p1 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

            LOG_MAC_802_16j_NT(mac80216j_NT_log_p1, *start_recv_time, *start_recv_time, get_type(), get_nid(), StartRX,
                    log_info->src_nid, get_nid(), burst_type, log_info->burst_len, log_info->channel_id);

            INSERT_TO_HEAP(logep, mac80216j_NT_log_p1->PROTO, mac80216j_NT_log_p1->Time + START, mac80216j_NT_log_p1);
#if LOG_DEBUG
            printf("BS: %llu ~ %llu %d->%d (StartRX)\n", GetCurrentTime(), GetCurrentTime(), log_info->src_nid, get_nid());
#endif

            /* log DropRX event */
            mac80216j_NT_log_t *mac80216j_NT_log_p2 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

            DROP_LOG_802_16j_NT(mac80216j_NT_log_p1, mac80216j_NT_log_p2, GetCurrentTime(), DropRX, DROP_COLL);

            INSERT_TO_HEAP(logep, mac80216j_NT_log_p2->PROTO, mac80216j_NT_log_p2->Time + ENDING, mac80216j_NT_log_p2);
#if LOG_DEBUG
            printf("BS: %llu ~ %llu %d->%d (DropRX)\n", *start_recv_time, GetCurrentTime(), log_info->src_nid, get_nid());
#endif
        }
        /********* LOG END *********/

        freePacket(epkt);
        return;
    }

    /********* LOG START *********/
    if (log_flag)
    {
        log_info = (struct LogInfo *) pkt->pkt_getinfo("loginfo");

        if (!log_info)
        {
            fprintf(stderr, "OFDMA_PMPBS_NT::recvHandler(): missing log information.\n");
            exit(1);
        }

        struct logEvent *logep      = NULL;
        uint8_t burst_type          = FRAMETYPE_MR_WIMAX_NT_PMP_ULAccessBURST;
        uint64_t *start_recv_time   = (uint64_t *)pkt->pkt_getinfo("srecv_t");

        /* log SuccessRX event */
        mac80216j_NT_log_t *mac80216j_NT_log_p2 = (mac80216j_NT_log_t *)malloc(sizeof(mac80216j_NT_log_t));

        LOG_MAC_802_16j_NT(mac80216j_NT_log_p2, GetCurrentTime(), *start_recv_time, get_type(), get_nid(), SuccessRX,log_info->src_nid, get_nid(), burst_type, log_info->burst_len, log_info->channel_id);

        INSERT_TO_HEAP(logep, mac80216j_NT_log_p2->PROTO, mac80216j_NT_log_p2->Time + ENDING, mac80216j_NT_log_p2);
#if LOG_DEBUG
        printf("BS: %llu ~ %llu %d->%d (SuccessRX)\n", *start_recv_time, GetCurrentTime(), log_info->src_nid, get_nid());
#endif
    }
    /********* LOG END *********/

    if (uiuc == CDMA_BWreq_Ranging) /* Process ranging code message */
    {
        fec = BPSK_;

        if (phyInfo->ulmap_ie.ie_12.rangMethod == 0)        // Initial or Handover Ranging over 2 symbols
        {
            uncodedLen = 2 * 18;
        }
        else if (phyInfo->ulmap_ie.ie_12.rangMethod == 1)   // Initial or Handover Ranging over 4 symbols
        {
            uncodedLen = 4 * 18;
        }
        else if (phyInfo->ulmap_ie.ie_12.rangMethod == 2)   // BW Request or Period Ranging over 1 symbol
        {
            uncodedLen = 1 * 18;
        }
        else                                                // BW Request or Period Ranging over 3 symbols
        {
            uncodedLen = 3 * 18;
        }

        /* Allocate a new packet to deliver ranging code message */
        pkt     = new Packet;
        assert(pkt);
        output  = pkt->pkt_sattach(uncodedLen);
        pkt->pkt_sprepend(output, uncodedLen);
        pkt->pkt_setflow(PF_RECV);

        /* Make bit error and copy to output */
        BER = _channelModel->computeBER(fec, phyInfo->SNR);
        _channelModel->makeBitError(input, uncodedLen, BER);
        memcpy(output, input, uncodedLen);

        /* Convey additional PHY information */
        pkt->pkt_addinfo("phyInfo", (char *) phyInfo, sizeof(struct PHYInfo));

        /* Deliver the ranging code message to upper layer */
        deliver_epkt            = createEvent();
        deliver_epkt->DataInfo_ = pkt;
        put(deliver_epkt, recvtarget_);
    }
    else
    {
        if (uiuc == CDMA_Alloc_IE)  /* Special message of CDMA allocation */
        {
            repe = phyInfo->ulmap_ie.ie_14.repeCode;
        }
        else /* Normal uplink burst */
        {
            repe = phyInfo->ulmap_ie.ie_other.repeCode;
        }

        /* Compute the data length */
        fec         = phyInfo->fec;
        codedLen    = pkt->pkt_getlen();
        uncodedLen  = _channelCoding->getUncodedBurstLen(codedLen, fec);

        /* Allocate a new packet */
        pkt         = new Packet;
        assert(pkt);
        output      = pkt->pkt_sattach(uncodedLen);
        pkt->pkt_sprepend(output, uncodedLen);
        pkt->pkt_setflow(PF_RECV);

        /* Make bit error and decode burst */
        BER = _channelModel->computeBER(fec, phyInfo->SNR);
        _channelModel->makeBitError(input, codedLen, BER);
        slots = _channelCoding->getNumCodedSlotWithRepe(codedLen, repeat_times_NT[repe], fec);
        _channelCoding->decode(input, output, codedLen, slots, repeat_times_NT[repe], fec);

        /* We obtain avgSNR by averaging 300 samples. This facilitates the  */
        /* profile selection in MAC, otherwise MAC will probably receive    */
        /* some extreme values and make bad selection.                      */
        char cm_obj_name[100]   = "";
        cm *cmobj               = NULL;

        sprintf(cm_obj_name, "Node%d_CM_LINK_%d", get_nid(), get_port());
        cmobj = (cm*)RegTable_.lookup_Instance(get_nid(), cm_obj_name);
        if (cmobj == NULL)
        {
            fprintf(stderr, "[%s] Node %d:: No CM this Instance!!\n\n", __func__, get_nid());
            exit(0);
        }

        avgSNR = 0;
        for (int i = 1;i <= 300; i++) 
        {
            SNR     = _channelModel->powerToSNR(cmobj->computePr(wphyinfo));
            avgSNR  += ((SNR - avgSNR) / i);
        }

        /* Convey additional PHY information */
        phyInfo->SNR = avgSNR;
        pkt->pkt_addinfo("phyInfo", (char *) phyInfo, sizeof(struct PHYInfo));

        /* Deliver the burst to upper layer */
        deliver_epkt            = createEvent();
        deliver_epkt->DataInfo_ = pkt;
        put(deliver_epkt, recvtarget_);
    }

    /* Free memory */
    freePacket(epkt);
    //epkt = NULL;
}

void OFDMA_PMPBS_NT::buildDLFP(struct DLFP_except_128 *frame_prefix, int dlmapLen)
{
    int numSlots = _channelCoding->getNumUncodedSlotWithRepe(dlmapLen, repeat_times_NT[REPETATION_CODEWORD_1], QPSK_1_2);

    frame_prefix->ch_bitmap = 0x3F;
    frame_prefix->rsv1      = 0;
    frame_prefix->repecode  = REPETATION_CODEWORD_1;    // no repetition coding on DL-MAP
    frame_prefix->coding    = CC;
    frame_prefix->len       = numSlots;
    frame_prefix->rsv2      = 0;
}

void OFDMA_PMPBS_NT::buildDLFP(struct DLFP_for_128 *frame_prefix, int dlmapLen)
{
    int numSlots = _channelCoding->getNumUncodedSlotWithRepe(dlmapLen, repeat_times_NT[REPETATION_CODEWORD_1], QPSK_1_2);

    frame_prefix->ch        = 1;
    frame_prefix->rsv       = 0;
    frame_prefix->repecode  = REPETATION_CODEWORD_1;
    frame_prefix->coding    = CC;
    frame_prefix->len       = numSlots;
}


void OFDMA_PMPBS_NT::buildRzonePrefix(struct RzonePrefix_except_128 *frame_prefix, int rmapLen)
{
    int numSlots = _channelCoding->getNumUncodedSlotWithRepe(rmapLen, repeat_times_NT[REPETATION_CODEWORD_1], QPSK_1_2);

    frame_prefix->Rzone_loc = _DLAccessSymbols;
    frame_prefix->ch_bitmap = 0x3F;
    frame_prefix->len_lsb       = numSlots % 4;
    frame_prefix->len_msb       = numSlots >> 2;
    frame_prefix->fec_code_type = 0x08;
    frame_prefix->repe_ind  = 0x00;     // 0: No repetition coding on R-MAP
}

void OFDMA_PMPBS_NT::saveDLMAP(char *dstbuf, int len)
{
    _dlmapBuf = dstbuf;
    _dlmapLen = len;
}


void OFDMA_PMPBS_NT::saveRMAP(char *dstbuf, int len)
{
    _rmapBuf = dstbuf;
    _rmapLen = len;
}

void OFDMA_PMPBS_NT::setStateBusy_access()
{
    uint64_t ticks = 0;
    recvState = Busy_access;    // send DL access burst
    //printf("Time:%llu OFDMA_PMPBS_NT::%s _DLAccessSymbols = %d\n",GetCurrentTime(),__func__,_DLAccessSymbols);
    MICRO_TO_TICK(ticks, (_DLAccessSymbols) * Ts);
    timerIdle->start(ticks, 0);

}

void OFDMA_PMPBS_NT::setStateBusy_relay()
{
    uint64_t ticks = 0;
    recvState = Busy_access;    // send DL relay burst
    //printf("Time:%llu OFDMA_PMPBS_NT::%s _DLRelaySymbols = %d\n",GetCurrentTime(),__func__,_DLRelaySymbols);
    //MICRO_TO_TICK(ticks, (_DLRelaySymbols)*Ts + _TTG*PS);
    MICRO_TO_TICK(ticks, (_DLRelaySymbols)*Ts + _TTG*PS);
    timerIdle->start(ticks, 0);
}

void OFDMA_PMPBS_NT::setStateIdle()
{
    recvState = Idle;
    //printf("Time:%llu OFDMA_PMPBS_NT::%s Idle\n",GetCurrentTime(),__func__);
}


