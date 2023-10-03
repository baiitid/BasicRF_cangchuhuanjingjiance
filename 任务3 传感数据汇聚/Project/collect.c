#include "hal_defs.h"  
#include "hal_cc8051.h"  
#include "hal_int.h"  
#include "hal_mcu.h"  
#include "hal_board.h"  
#include "hal_led.h"  
#include "hal_rf.h"  
#include "basic_rf.h"  
#include "hal_uart.h"   
#include "hal_pwm.h"   
#include "UART_PRINT.h"  
#include <stdlib.h>  
#include <string.h>  
#include <stdio.h>  


/******通讯协议相关*******/  
#define START_HEAD    0xCC//帧头  
#define CMD_READ      0x01//读传感器数据  
#define SENSOR_TEMP   0x01//温度  
#define SENSOR_RH     0x02//湿度  
#define SENSOR_FIRE   0x03//火焰  


/*****点对点通讯地址设置******/
#define RF_CHANNEL                16         // 频道 11~26
#define PAN_ID                    0xD0C2     //网络id
#define MY_ADDR                   0xB4F3     //本机模块地址
/********无线数据缓存********/
#define MAX_SEND_BUF_LEN  128
#define MAX_RECV_BUF_LEN  128
static uint8 pTxData[MAX_SEND_BUF_LEN]; //定义无线发送缓冲区的大小
static uint8 pRxData[MAX_RECV_BUF_LEN]; //定义无线接收缓冲区的大小
/**************************************************/
static basicRfCfg_t basicRfConfig;
//使LED n 闪烁 time 毫秒
#define FlashLed(n,time) do{\
      halLedSet(n);\
      halMcuWaitMs(time);\
      halLedClear(n);\
      }while(0)

// 无线RF初始化
void ConfigRf_Init(void)
{
    basicRfConfig.panId       =   PAN_ID;        //zigbee的ID号设置
    basicRfConfig.channel     =   RF_CHANNEL;    //zigbee的频道设置
    basicRfConfig.myAddr      =  MY_ADDR;   //设置本机地址
    basicRfConfig.ackRequest  =   TRUE;          //应答信号
    while(basicRfInit(&basicRfConfig) == FAILED); //检测zigbee的参数是否配置成功
    basicRfReceiveOn();                // 打开RF
}

/****************************************************************************************** 
*函数：uint8 GetHexStr(uint8 *input, uint8 len, uint8 *output) 
*功能：生成数组的16进制形式的字符串格式,成员间以空格隔开 
*     例如:由{0xA1,0xB2,0xC3}生成"A1 B2 C3" 
*输入：uint8 *input-指向输入缓存区, uint8 len-输入数据的字节长度 
*输出：uint8 *output-指向输出缓存区 
*返回：返回生成字符串的长度 
*特殊说明：无 
******************************************************************************************/
uint8 GetHexStr(uint8 *input, uint8 len, uint8 *output)  
{  
   char str[128];  
   memset(str, '\0', 128);  
   for(uint8 i=0; i<len; i++)  
   {  
      sprintf(str+i*3,"%02X ", *input);  //注意，字符串间以空格隔开，"%02X "双引号中有一个空格
      input++;  
   }  
   strcpy((char *)output, (const char *)str);  
   return strlen((const char *)str);  
}  


/************************************************************************************ 
*函数：uint8 CheckSum(uint8 *buf, uint8 len) 
*功能：计算校验和 
*输入：uint8 *buf-指向输入缓存区, uint8 len输入数据字节个数 
*输出：无 
*返回：返回校验和 
*特殊说明：无 
*************************************************************************************/  
uint8 CheckSum(uint8 *buf, uint8 len)  
{  
   uint8 temp = 0;  
   while(len--)  
   {  
      temp += *buf;  
      buf++;  
   }  
   return (uint8)temp;  
}  


void main(void)  
{  
   uint16 len = 0;  
   uint32 TimCnt = 0;  
   int8 brightness = 0;  
   uint8 flag = 0;//0-向下渐变暗，1-向上渐变亮  
   halBoardInit();  //模块相关资源的初始化  
   ConfigRf_Init(); //无线收发参数的配置初始化   
   while(1)  
   {  
   /************************呼吸灯进程***********************/  
      if(TimCnt++>1024)  
      {  
         TimCnt = 0;  
         if(flag)  
         {  
            brightness ++;  
            if(brightness >= 90)  
            {flag = 0;}//向下渐变暗  
         }  
         else  
         {  
            brightness --;  
            if(brightness <= 10)  
            {flag = 1;}//向上渐变亮  
         }  
         TIM1_PwmInit(1,brightness);  
       }  
        /*************************无线数据接收处理进程******************/
        if(basicRfPacketIsReady())   //查询有没收到无线信号
        {
            FlashLed(2,100);//无线接收指示，LED2亮100ms
            //接收无线数据
            len = basicRfReceive(pRxData, MAX_RECV_BUF_LEN, NULL);
#ifdef CC2530_DEBUG
            uint8 pos = 0;
            char DebugOutput[256];
            memset(DebugOutput, '\0', 256);
            GetHexStr((uint8 *)pRxData, len, (uint8 *)DebugOutput); 
            uart_printf("接收到原始无线RF数据：%s\r\n",DebugOutput);
            //数据解析
            uint8 check = 0;
            if((pRxData[2]+1) > MAX_RECV_BUF_LEN)//数据长度不符合规则
            {
              continue;
            }
            check = CheckSum((uint8 *)pRxData, pRxData[2]);
            if((pRxData[0] == START_HEAD) && (check == pRxData[pRxData[2]]))//帧头正确且校验通过
            {
              if(pRxData[3]==1)//一个传感数据
              {
                switch(pRxData[4])
                {
                  case(SENSOR_TEMP):
                    uart_printf("当前温度：%d℃\r\n",pRxData[5]);
                    break;//温度
                  case(SENSOR_RH):
                    uart_printf("当前相对湿度：%d%%\r\n",pRxData[5]);
                    break;//湿度
                  case(SENSOR_FIRE):
                    uart_printf("当前红外线(火焰)强度：%dmV\r\n",(((uint16)pRxData[5])<<8)+pRxData[6]);
                    break;//火焰
                  default:break;
                }
              }
              else if(pRxData[3]==2)//2个传感数据
              {
                pos=0;
                //第一组传感数据
                uart_printf("第一组：");
                switch(pRxData[4])
                {
                  case(SENSOR_TEMP):
                    uart_printf("当前温度：%d℃\r\n",pRxData[5]);
                    break;//温度
                  case(SENSOR_RH):
                    uart_printf("当前相对湿度：%d%%\r\n",pRxData[5]);
                    break;//湿度
                  case(SENSOR_FIRE):
                    uart_printf("当前红外线(火焰)强度：%dmV\r\n",(((uint16)pRxData[5])<<8)+pRxData[6]);
                    pos=1;
                    break;//火焰
                  default:break;
                }
                //第二组传感数据
                uart_printf("第二组：");
                switch(pRxData[6+pos])
                {
                  case(SENSOR_TEMP):
                    uart_printf("当前温度：%d℃\r\n",pRxData[7+pos]);
                    break;//温度
                  case(SENSOR_RH):
                    uart_printf("当前相对湿度：%d%%\r\n",pRxData[7+pos]);
                    break;//湿度
                  case(SENSOR_FIRE):
                    uart_printf("当前红外线(火焰)强度：%dmV\r\n",(((uint16)pRxData[7+pos])<<8)+pRxData[8+pos]);
                    break;//火焰
                  default:break;
                }
              }
            }
            else
            {
              continue;
            }
#else
            halUartWrite((uint8 *)pRxData, len);//串口透传
#endif /*CC2530_DEBUG*/ 
       }  
   }  
}