/**
 ******************************************************************************
 * @file			: module_api_inf.c
 * @brief			: api for application based on at
 ******************************************************************************
 *
 * History:      <author>          <time>        <version>
 *               yougaliu          2019-3-20        1.0
 * Desc:          ORG.
 * Copyright (c) 2019 Tencent. 
 * All rights reserved.
 ******************************************************************************
 */

#include <stdio.h>
#include <string.h>
#include "module_api_inf.h"
#include "qcloud_iot_api_export.h"
#include "at_client.h"
#include "utils_timer.h"



#define  MQTT_CON_FLAG 			(1<<0)
#define  MQTT_SUB_FLAG			(1<<1)
#define  MQTT_PUB_FLAG			(1<<2)
#define  MQTT_STATE_FLAG		(1<<3)
#define  WIFI_CON_FLAG 			(1<<4)
#define  N21_REG_FLAG 			(1<<5)

static uint32_t sg_flags = 0;


static void setFlag(uint32_t flag)
{
	sg_flags |= flag&0xffffffff;
}

static void clearFlag(uint32_t flag)
{
	sg_flags &= (~flag)&0xffffffff;
}

static uint32_t getFlag(void)
{
	return sg_flags;
}


/*if os support event we can use event instand*/
static bool waitFlag(uint32_t flag, uint32_t timeout)
{
	Timer timer;
	bool Ret = false;
	
	countdown_ms(&timer, timeout);
	
	do
	{
		if(flag == (getFlag()&flag))
		{
			Ret = true;
			break;
		}
		HAL_SleepMs(1);
	}while(!expired(&timer));

	return Ret;

}

#ifdef MODULE_TYPE_WIFI
static void urc_wifi_conn_func(const char *data, uint32_t size)
{
	Log_d("receve wifi conn urc(%d):%s", size, data);
	setFlag(WIFI_CON_FLAG);
}
#else
static void urc_n21_reg_func(const char *data, uint32_t size)
{
	Log_d("receve reg urc(%d):%s", size, data);

	if(strstr(data,"+CREG: 0,1"))
	{
		setFlag(N21_REG_FLAG);
	}
	else
	{
		clearFlag(N21_REG_FLAG);
	}
}
#endif



static void urc_pub_recv_func(const char *data, uint32_t size)
{
	//Log_d("receve pub_msg(%d):%s", size, data);
	//HAL_DelayMs(200); 
	deliver_message(data, size);
}

static void urc_mqtt_conn_func(const char *data, uint32_t size)
{
	Log_d("receve mqtt conn urc(%d):%s", size, data);
	setFlag(MQTT_CON_FLAG);
}


static void urc_discon_func(const char *data, uint32_t size)
{
	Log_d("receve disconnect urc(%d):%s", size, data);
	hal_thread_destroy(NULL);
}

static void urc_mqtt_sub_func(const char *data, uint32_t size)
{
	Log_d("receve mqtt sub urc(%d):%s", size, data);
	setFlag(MQTT_SUB_FLAG);
}


static void urc_mqtt_state_func(const char *data, uint32_t size)
{
	//Log_d("receve mqtt state urc(%d):%s", size, data);
	
	if(strstr(data,"+TCMQTTSTATE:1"))
	{
		setFlag(MQTT_STATE_FLAG);
	}
	else
	{
		clearFlag(MQTT_STATE_FLAG);
	}
}

static void urc_ota_status_func(const char *data, uint32_t size)
{
	Log_d("receve ota status urc(%d):%s", size, data);
}




static at_urc urc_table[] = {

        {"+TCMQTTRCVPUB:", "\r\n", urc_pub_recv_func},
        {"+TCMQTTDISCON",  "\r\n", urc_discon_func},
        {"+TCMQTTSUB:",    "\r\n", urc_mqtt_sub_func},
        {"+TCMQTTSTATE:",  "\r\n", urc_mqtt_state_func},
        {"+TCOTASTATUS:",  "\r\n", urc_ota_status_func},
        {"+TCMQTTCONN:",   "\r\n", urc_mqtt_conn_func},
        
#ifdef MODULE_TYPE_WIFI
        {"WIFI CONNECTED",   "\r\n", urc_wifi_conn_func},
#else
		{"+CREG:",   "\r\n", urc_n21_reg_func},
#endif
       
};


eAtResault module_init(eModuleType eType)
{
	eAtResault ret;
	at_client_t p_client;

	p_client = at_client_get();

	if(NULL == p_client)
	{
		Log_e("no at client get");
		ret = AT_ERR_FAILURE;
		goto exit; 
	}

	if(AT_STATUS_INITIALIZED == p_client->status)
	{
		Log_e("at client has been initialized");
		ret = AT_ERR_FAILURE;
		goto exit;
	}
	
	
    /* initialize AT client */
    ret = at_client_init(p_client);
	if(AT_ERR_SUCCESS != ret)
 	{
		Log_e("at client init fail,ret:%d", ret);
		goto exit;
	}
	else
	{
		Log_d("at client init success");
	}

	/*module power on should after at client init for AT uart irq use ringbuff*/
	if(AT_ERR_SUCCESS != module_power_on())
 	{
		Log_e("module power on fail");
		goto exit;
	}
	
    /* register URC data execution function  */
    at_set_urc_table(p_client, urc_table, sizeof(urc_table) / sizeof(urc_table[0]));


	Log_d("urc table addr:%p, size:%d", p_client->urc_table, p_client->urc_table_size);
	for(int i=0; i < p_client->urc_table_size; i++)
	{
		Log_d("%s",p_client->urc_table[i].cmd_prefix);
	}

exit:

    return ret;
}


eAtResault module_handshake(uint32_t timeout)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	resp = at_create_resp(256, 0, CMD_TIMEOUT_MS);
	if (resp == NULL)
	{
		Log_e("No memory for response object!");
		return AT_ERR_RESP_NULL;
	}
		
    /* disable echo */
    if(AT_ERR_SUCCESS != at_exec_cmd(resp, "ATE0"))
    {
    	Log_e("cmd ATE0 exec err");
		result = AT_ERR_FAILURE;
		//goto exit;
	}

    /* get module version */
	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+GMR"))
    {
    	Log_e("cmd AT+GMR exec err");
		result = AT_ERR_FAILURE;
		//goto exit;
	}

	Log_d("Module info(%d):", resp->line_counts);
    /* show module version */
    for (int i = 0; i < resp->line_counts - 1; i++)
    {
        HAL_Printf("\n\r%s", at_resp_get_line(resp, i + 1));
    }

//exit:

	if(resp)
	{
		at_delete_resp(resp);
	}

	return result;
}

/*
* config dev info to module, do this operate only once in factroy is suggested
*/
eAtResault module_info_set(DeviceInfo *pInfo, eTlsMode eMode)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);
	
	/* Set dev info */
	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCDEVINFOSET=%d,\"%s\",\"%s\",\"%s\"",\
									eMode,pInfo->product_id, pInfo->device_name, pInfo->devSerc))								
    {
    	Log_e("cmd AT+TCDEVINFOSET exec err");
		result = AT_ERR_FAILURE;
		//goto exit;
	}

	if(resp)
	{
		at_delete_resp(resp);
	}
	
    return result;
}

/* mqtt setup connect */
eAtResault module_mqtt_conn(MQTTInitParams init_params)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);

	/* clear sub flag*/
    clearFlag(MQTT_CON_FLAG);
	/* Start mqtt connect */
	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTCONN=%d,%d,%d,%d,%d",init_params.tlsmod,\
										init_params.command_timeout, init_params.keep_alive_interval_ms,\
										init_params.clean_session,  init_params.auto_connect_enable))
	{
		Log_e("cmd AT+TCMQTTCONN exec err");
		result = AT_ERR_FAILURE;
		//goto exit;
	}

	if(!waitFlag(MQTT_CON_FLAG, CMD_TIMEOUT_MS))
	{
		result = AT_ERR_FAILURE;
	}
	
	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}

/* mqtt disconn */
eAtResault module_mqtt_discon(void)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);	
	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTDISCONN"))			
	{
		Log_e("cmd AT+TCMQTTDISCONN exec err");
		result = AT_ERR_FAILURE;
	}

	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}

/* mqtt pub msg */
eAtResault module_mqtt_pub(const char *topic, QoS eQos, char *payload)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	if(strlen(payload) > MAX_PAYLOAD_LEN_PUB){
		Log_d("PUBL cmd used");
		result = module_mqtt_publ(topic, eQos, payload);
	}else{
		resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);		
		if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTPUB=\"%s\",%d,\"%s\"",topic,eQos,payload))	
		{
			Log_e("cmd AT+TCMQTTPUB exec err");
			result = AT_ERR_FAILURE;
		}

		if(resp)
		{
			at_delete_resp(resp);
		}
	}
	
	return result;
}

eAtResault module_mqtt_publ(const char *topic, QoS eQos, char *payload)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;
	int len;
	
	resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);
	at_set_end_sign('>');

	result = at_exec_cmd(resp, "AT+TCMQTTPUBL=\"%s\",%d,%d",topic, eQos, strlen(payload));
	if(AT_ERR_SUCCESS !=  result)	
	{
		Log_e("cmd AT+TCMQTTPUBL exec err");
		goto exit;
	}
	
	len = at_client_send(at_client_get(), payload, strlen(payload));
	if(strlen(payload) !=  len)	
	{
		result = AT_ERR_SEND_DATA;
		Log_e("send data err");
	}

exit:

	if(resp)
	{
		at_delete_resp(resp);
	}

	at_set_end_sign(0);
    return AT_ERR_SUCCESS;
}


eAtResault module_mqtt_sub(char *topic, QoS eQos, OnMessageHandler cb, void *contex)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;
	SubscribeParams SubsParams;

	SubsParams.topicFilter = topic;
	SubsParams.eqos = eQos;
	SubsParams.fp = cb;
	SubsParams.context = contex;

	
	result = register_sub_topic(&SubsParams);
	if(AT_ERR_SUCCESS !=  result)
	{
		Log_e("register sub topic err,ret:%d",result);
		return result;
	}

	/* clear sub flag*/
   clearFlag(MQTT_SUB_FLAG);

	resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);	

	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTSUB=\"%s\",%d",topic,eQos))	
	{
		Log_e("cmd AT+TCMQTTSUB exec err");
		result = AT_ERR_FAILURE;
	}

	if(!waitFlag(MQTT_SUB_FLAG, CMD_TIMEOUT_MS))
	{
		Log_e("%s sub fail", topic);
		result = AT_ERR_FAILURE;
	}
	else
	{
		Log_d("%s sub success", topic);
	}

	if(resp)
	{
		at_delete_resp(resp);
	}

	clearFlag(MQTT_SUB_FLAG);

	
	return result;
}

eAtResault module_mqtt_unsub(const char *topic)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	resp = at_create_resp(64, 0, CMD_TIMEOUT_MS);
	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTUNSUB=\"%s\"",topic))		
	{
		Log_e("cmd AT+TCMQTTUNSUB exec err");
		result = AT_ERR_FAILURE;
	}

	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}


eAtResault module_mqtt_state(eMqtt_State *pState)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;
	//int argc, state;
	//const char *req_expr = "+TCMQTTSTATE:%u";

	resp = at_create_resp(256, 0, CMD_TIMEOUT_MS);
	
	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTSTATE?")) 			
	{
		Log_e("cmd AT+TCMQTTSTATE exec err");
		result = AT_ERR_FAILURE;
	}

	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}


bool IOT_MQTT_IsConnected(void) 
{
	eMqtt_State eState = eDISCONNECTED;
	eAtResault result;
	
	result = module_mqtt_state(&eState);
	if(AT_ERR_SUCCESS != result)
	{
		Log_e("Get mqtt state fail,ret:%d", result);
		return false;
	}
	else
	{
		//Log_d("con_state:%u", (sg_flags&MQTT_STATE_FLAG)>>3);
	}

	return ((getFlag()&MQTT_STATE_FLAG) > 0)?true:false;	
}

#ifdef MODULE_TYPE_WIFI
eAtResault wifi_connect(const char *ssid, const char *pw)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	/* clear sub flag*/
    clearFlag(WIFI_CON_FLAG);
	resp = at_create_resp(128, 0, CMD_TIMEOUT_MS);

	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+CWJAP=\"%s\",\"%s\"",ssid, pw))		
	{
		Log_e("cmd AT+CWJAP exec err");
		//result = AT_ERR_FAILURE;
	}

	if(!waitFlag(WIFI_CON_FLAG, CMD_TIMEOUT_MS))
	{
		Log_e("wifi connect fail");
		result = AT_ERR_FAILURE;
	}

	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}

eAtResault wifi_set_test_server_ip(const char *host)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;

	/* clear sub flag*/
    clearFlag(WIFI_CON_FLAG);
	resp = at_create_resp(128, 0, CMD_TIMEOUT_MS);

	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+TCMQTTSRV=\"%s\"",host))		
	{
		Log_e("cmd AT+TCMQTTSRV exec err");
		result = AT_ERR_FAILURE;
	}

	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}

#else
#define RETRY_TIMES		3
eAtResault N21_net_reg(void)
{
	eAtResault result = AT_ERR_SUCCESS;
	at_response_t resp = NULL;
	int retry = 0;

	/* clear sub flag*/
	clearFlag(N21_REG_FLAG);
	resp = at_create_resp(128, 0, CMD_TIMEOUT_MS);

	/*module register network need sometime, adjust RETRY_TIMES for your product*/
	while(retry++ < RETRY_TIMES)
	{
		if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+CREG?"))		
		{
			Log_e("cmd AT+CREG? exec err");
			result = AT_ERR_FAILURE;
			goto exit;
		}
		
		if(!waitFlag(N21_REG_FLAG, CMD_TIMEOUT_MS))
		{
			continue;
		}else{
			break;
		}
	}

	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+CREG?"))		
	{
		Log_e("cmd AT+CREG? exec err");
		result = AT_ERR_FAILURE;
		goto exit;
	}
	
	if(!waitFlag(N21_REG_FLAG, CMD_TIMEOUT_MS/5))
	{
		Log_e("N21 register network timeout");
		result = AT_ERR_FAILURE;
		goto exit;
	}


	if(AT_ERR_SUCCESS !=  at_exec_cmd(resp, "AT+XIIC=1"))		
	{
		Log_e("cmd AT+XIIC exec err");
		result = AT_ERR_FAILURE;
	}

exit:

	if(resp)
	{
		at_delete_resp(resp);
	}
	
	return result;
}


#endif

/*
*模组联网（NB/2/3/4G注册网络）、wifi配网（一键配网/softAP）暂时很难统一,需要用户根据具体模组适配。
* 开发者参照 module_handshake API使用AT框架的API和模组交互，实现适配。
*/	
eAtResault module_register_network(eModuleType eType)
{
	eAtResault result = AT_ERR_SUCCESS;
	
#ifdef MODULE_TYPE_WIFI	
	if(eType == eMODULE_ESP8266)
	{
		#define WIFI_SSID	"youga_wifi"
		#define WIFI_PW		"Iot@2018"

		/*此处示例传递热点名字直接联网，通常的做法是特定产品根据特定的事件（譬如按键）触发wifi配网（一键配网/softAP）*/
		result = wifi_connect(WIFI_SSID, WIFI_PW);
		if(AT_ERR_SUCCESS != result)
		{
			Log_e("wifi connect fail,ret:%d", result);	
		}
	}
#else	
	if(eType == eMODULE_N21)
	{
	
		/*模组网络注册、或者wifi配网需要用户根据所选模组实现*/			
		result = N21_net_reg();
		if(AT_ERR_SUCCESS != result)
		{
			Log_e("N21 register network fail,ret:%d", result);	
		}	
	}
#endif	


	return result;
}




