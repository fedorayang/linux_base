/*********************************************************************
    Copyright (c) 2008 - 2010 Embedded Internet Solutions, Inc
    All rights reserved. You are not allowed to copy or distribute
    the code without permission.
    This is the demo implenment of the base Porting APIs needed by 
    iPanel MiddleWare. 
    Maybe you should modify it accorrding to Platform.
    
    Note: the "int" in the file is 32bits

	Implementer : yuxj
	date-time	: 2009-7-30
	
    History:
	version			date		 	name		desc
	v3.0.1.0    	2009-07-30 		yuxj		modify and debug in
												sdk7105 v1.2

*********************************************************************/
#include "stapp_main.h"
#include "ipanel_typedef.h"
#include "ipanel_base.h"
#include "ipanel_os.h"
#include "ipanel_storage.h"
#include "ipanel_vfs.h"
#include "ipanel_porting_event.h"
#include  "porting_event.h"
/*below is for init_hotplug_sock()*/
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <ctype.h> 
#include <sys/un.h> 
#include <sys/ioctl.h> 
#include <sys/mount.h>
#include <sys/socket.h> 
#include <linux/types.h> 
#include <linux/netlink.h> 
#include <errno.h> 
#include <unistd.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
//#include "private_define.h"
#include "pri_port_config.h"
#include "pri_port_misc.h"
#include "ipanel_configs.h"
#include "pri_port_syscall.h"

/*********************************************************************/
#define IPANEL_USB_NAME_LEN_MAX 32 /*与VFS_MAX_STRING  保持一致*/
#define IPANEL_DEVICES_MAX 2 /*物理存储设备*/
#define IPANEL_MOUNTS_MAX 8 /*与VFS  保持一致,  通常足够*/
#define IPANEL_MAX_LOGICS_PER_DEV 6 /*每个物理设备最多支持6  个逻辑设备*/

#define UEVENT_BUFFER_SIZE 2048 

#define IPANEL_VFS_DEVICE_MASS_STORAGE 5
#define IPANEL_VFS_DEVICE_REMOVABLE 6

#define BASE_VOLUME_H	('D')	
#define BASE_VOLUME_L	('d')

#define IPANEL_VFS_DEFAULT_MOUNT_MODE "rw"
static int usbflag = 0;
#define HOT_PLUG_SOCKET 1 
#define PORTING_DBG(...) //printf(__VA_ARGS__)  
#if 0
struct statfs {
	unsigned int f_type;
	unsigned int f_bsize;
	unsigned int f_blocks;
	unsigned int f_bfree;
	unsigned int f_bavail;
	unsigned int f_files;
	unsigned int f_ffree;
	__kernel_fsid_t f_fsid;
	unsigned int f_namelen;
	unsigned int f_frsize;
	unsigned int f_spare[5];
};
#endif
typedef struct LOGIC_INFO
{
	/*所属物理设备名*/
	BYTE_T devName[IPANEL_USB_NAME_LEN_MAX];
	
	/*mount  后的名称*/
	BYTE_T mountedName[IPANEL_USB_NAME_LEN_MAX];
	
	/*卷标名称*/
	BYTE_T volumeName[IPANEL_USB_NAME_LEN_MAX];
	
	/*模式名称*/
	BYTE_T modeName[IPANEL_USB_NAME_LEN_MAX];
	
	/*文件系统名称*/
	BYTE_T fileSystem[IPANEL_USB_NAME_LEN_MAX];
	
	/*逻辑设备号'D'/'E'/'F'  等*/
	CHAR_T logic;	
	
	/*在物理设备上的编号,  从1  ( 0  另有意义)  开始,  对应于物理设备的逻辑设备列表的(index + 1)*/
	INT32_T subIndex;
	
	/*是否正在占用/  有效*/
	CHAR_T using;
}IPANEL_LOGIC_INFO_t;

typedef struct
{
	CHAR_T volumeName[IPANEL_USB_NAME_LEN_MAX]; 
}PARTITION_t;

typedef struct PARTITIONS_LIST
{
	/*逻辑分区列表*/
	PARTITION_t partitions[IPANEL_MAX_LOGICS_PER_DEV];		
	
	/*分区数量*/
	INT32_T partitionsCnt;
}PARTITIONS_LIST_t;

typedef struct DEVICE_INFO 
{
	/*设备名称*/
	CHAR_T devName[IPANEL_USB_NAME_LEN_MAX];   	
	
	/*设备类型名称*/
	CHAR_T typeName[IPANEL_USB_NAME_LEN_MAX];  	
	
	/*存储设备的逻辑设备列表*/
	PARTITIONS_LIST_t partitionsList;
	
	/*设备类型*/
	UINT32_T type;			
	
	/*系统安排的序号,  基于0  ,  物理设备列表序号*/
	INT32_T index;			
	
	/*是否被占用*/
	BYTE_T using;			
}IPANEL_DEVICE_INFO_t;

/*********************************************************************/


/*********************************************************************/
static IPANEL_DEVICE_INFO_t m_devices[IPANEL_DEVICES_MAX];/*设备列表*/
static INT32_T m_device_count = -1;/*设备数量*/
static IPANEL_LOGIC_INFO_t m_mounts[IPANEL_MOUNTS_MAX];/*挂载的设备列表*/
static INT32_T m_mount_count = -1;/*挂载的设备数量*/

static UINT32_T 	m_queue = 0;
static UINT32_T 	m_task 	= 0;
static UINT32_T	m_mutex = 0;
static UINT32_T 	m_running = 0;/*线程是否运行*/
static UINT32_T 	m_exit = 1;/*是否退出线程*/
static INT32_T 	m_freshed = 0;/*usb是否有变化*/
extern g_ipanel_handle;
/*********************************************************************/


/*********************************************************************/

/*********************************************************************/


/*********************************************************************/
static void ipanel_vfs_trave_usb_storage_devices();
static INT32_T ipanel_vfs_push_vod_real_path(CHAR_T *filename, INT32_T nameLen, CHAR_T *newpath);
static INT32_T ipanel_vfs_add_dev(const CHAR_T* msgStr);
static INT32_T ipanel_vfs_add_mnts(const CHAR_T* msgStr);
static INT32_T ipanel_vfs_del_dev(const CHAR_T* msgStr);
static INT32_T ipanel_vfs_del_mnts(const CHAR_T* mntName);
static INT32_T ipanel_vfs_get_logic_info(INT32_T devIndex, INT32_T logIndex, IPANEL_STORAGE_DEV_INFO* info);
static INT32_T ipanel_vfs_get_a_dev_info(INT32_T devIndex, IPANEL_STORAGE_DEV_INFO* info);
static INT32_T ipanel_vfs_init_hotplug_sock(); 
static INT32_T ipanel_vfs_task();
static int test_usb_dev_proc();

/*********************************************************************/

INT32_T ipanel_vfs_init()
{
	ST_ErrorCode_t re = ST_NO_ERROR;
	
	m_device_count = 0;	
	memset(m_devices, 0x00, sizeof(m_devices));
	m_mount_count = 0;
	memset(m_mounts, 0x00, sizeof(m_mounts));
	
#if HOT_PLUG_SOCKET
	//ipanel_vfs_trave_usb_storage_devices();
#else
	m_freshed = 0;
#endif
	
	/*创建内部队列*/
	m_queue = ipanel_porting_queue_create("QUEU", 16, IPANEL_TASK_WAIT_FIFO);
	if (m_queue <= 0)
	{
		PORTING_DBG("[ipanel_vfs_init] ipanel_porting_queue_create() failed!\n");
		return IPANEL_ERR;
	}
	//g_sys_gbl.msg_queue_handle = m_queue;
	/*创建信号量,  这里做互斥量使用*/
	m_mutex = ipanel_porting_sem_create("MUTE", 1, IPANEL_TASK_WAIT_FIFO);
	if(m_mutex == 0)
	{
		ipanel_porting_queue_destroy(m_queue);
		m_queue = 0;
		PORTING_DBG ("[ipanel_vfs_init] ipanel_porting_sem_create() failed!\n");
		return IPANEL_ERR;
	}
	
	/*创建任务*/
	m_running = 1;
	m_exit = 0;
	m_task = ipanel_porting_task_create(TASK_USB_NAME, 
										(IPANEL_TASK_PROC)ipanel_vfs_task, 
										NULL, 
										TASK_USB_PRIO, 
										TASK_USB_STACK_SIZE);
	if (m_task <= 0)
	{
		m_exit = 1;
		m_running = 0;
		ipanel_porting_queue_destroy(m_queue);
		m_queue = 0;
		ipanel_porting_sem_destroy(m_mutex);
		m_mutex = 0;
		PORTING_DBG("[ipanel_vfs_init] ipanel_porting_task_create() failed!\n");
		return IPANEL_ERR;
	}
	
	/*文件系统初始化*/
	re = VFS_Init();
	if (re != ST_NO_ERROR)
	{
		m_exit = 1;
		m_running = 0;
		ipanel_porting_task_destroy(m_task);
		m_task = 0;
		ipanel_porting_queue_destroy(m_queue);
		m_queue = 0;
		ipanel_porting_sem_destroy(m_mutex);
		m_mutex = 0;
		PORTING_DBG ("[ipanel_vfs_init] VFS_Init() failed! ErrCode = 0x%x\n", re);
		return IPANEL_ERR;
	}
	
	return IPANEL_OK;
}

void ipanel_vfs_exit()
{
	ST_ErrorCode_t re = ST_NO_ERROR;
	
	if(m_running == 1)
	{
		/*通知线程退出*/
		m_running = 0;
		
		/*等待线程退出*/
		while(m_exit == 0)
			ipanel_porting_task_sleep(1);
		
		/*销毁线程*/
		if(m_task != 0)
		{
			ipanel_porting_task_destroy(m_task);
			m_task = 0;
		}
		
		m_exit = 1;
	}
	
	/*销毁队列*/	
	if (m_queue > 0)
	{
		ipanel_porting_queue_destroy(m_queue);
		m_queue = 0;
		PORTING_DBG("[ipanel_st_vfs_exit] destroy queue!\n");
	}
	
	/*销毁信号量(互斥)*/	
	if (m_mutex > 0)
	{
		ipanel_porting_sem_destroy(m_mutex);
		m_mutex = 0;
		PORTING_DBG("[ipanel_st_vfs_exit] destroy queue!\n");
	}
	
	/*文件系统销毁*/
	re = VFS_Term();
	if (re != ST_NO_ERROR)
	{
		PORTING_DBG("[ipanel_vfs_exit] VFS_Term() failed! ErrCode = 0x%x\n", re);
	}
	
	memset(m_mounts, 0x00, sizeof(m_mounts));
	m_mount_count = -1;
	memset(m_devices, 0x00, sizeof(m_devices));
	m_device_count = -1;
}

/*主要用于检测存储设备的可用性*/
INT32_T ipanel_vfs_get_mount_count()
{
	ipanel_porting_sem_wait(m_mutex, IPANEL_WAIT_FOREVER);
	ipanel_porting_sem_release(m_mutex);
	return m_mount_count;
}

INT32_T ipanel_vfs_get_storage_devs_count()
{
	ipanel_porting_sem_wait(m_mutex, IPANEL_WAIT_FOREVER);
	ipanel_porting_sem_release(m_mutex);
	return m_device_count;
}

/*info->index == 0  如何定义?  查询所有存储设备的信息和?  测试显示:  不对*/
/*info->subidx == 0  查询整个物理设备的信息*/
INT32_T ipanel_vfs_get_dev_info(IPANEL_STORAGE_DEV_INFO* info)
{
	INT32_T nRet = 0;

	if(info == NULL)
	{
		return IPANEL_ERR;
	}
	
	ipanel_porting_sem_wait(m_mutex, IPANEL_WAIT_FOREVER);
	
	/*查询单个设备的总信息*/
	if(info->subidx == 0)
	{
		nRet = ipanel_vfs_get_a_dev_info(info->index, info);
		if (nRet == 0)
		{
			ipanel_porting_sem_release(m_mutex);
			PORTING_DBG("[ipanel_vfs_get_dev_info] --[%d]----[%d]----[%d]\n" , info->free, info->size, info->type);
			return IPANEL_OK;
		}
		else
		{
			ipanel_porting_sem_release(m_mutex);
			PORTING_DBG("[ipanel_vfs_get_dev_info] -------failed--------\n");
			return IPANEL_ERR;
		}	
	}
	
	/*查询逻辑设备信息*/
	else
	{
		nRet = ipanel_vfs_get_logic_info(info->index, info->subidx, info);
		if (nRet == 0)
		{
			ipanel_porting_sem_release(m_mutex);
			PORTING_DBG("[ipanel_vfs_get_dev_info] --[%c]----[%d]----[%d]----[%s]----[%d]----\n" , 
				info->logic, info->free, info->size, info->name, info->type);
			return IPANEL_OK;
		}
		else
		{
			ipanel_porting_sem_release(m_mutex);
			PORTING_DBG("[ipanel_vfs_get_dev_info] -------failed--------\n");
			return IPANEL_ERR;
		}	
	}

	return IPANEL_OK;
}

INT32_T ipanel_vfs_real_path(CHAR_T *filename, CHAR_T *newpath)
{
	INT32_T index = 0, offset = 0, filenameLen = 0;
	
	if ((filename == NULL) || (newpath == NULL))
	{
		return IPANEL_ERR;
	}
	
	/*输入文件名长度*/
	filenameLen = strlen(filename);
	if (filenameLen == 0)
	{
		return IPANEL_ERR;
	}
	
	/*统一格式:  d:/tr/df/gj/wo.txt*/
	for (index = 0; index < filenameLen; index ++)
	{
		if (filename[index] == '\\')
			filename[index] = '/';
	}

	ipanel_porting_sem_wait(m_mutex, IPANEL_WAIT_FOREVER);
	/*针对push_vod*/
	if(ipanel_vfs_push_vod_real_path(filename, filenameLen, newpath) == IPANEL_OK)
	{
		ipanel_porting_sem_release(m_mutex);
		return IPANEL_OK;
	}
	else
	{
		ipanel_porting_sem_release(m_mutex);
		return IPANEL_ERR;
	}
	
	/*路径映射/  转换*/
	if ((filenameLen > 2) && (filename[1] == ':') && (filename[2] == '/'))/*f:/*/
	{
		/*盘符是小写字母时转换成大写字母*/
		if ((filename[0] >= BASE_VOLUME_L) && (filename[0] < (BASE_VOLUME_L+IPANEL_MOUNTS_MAX)))
		{
			offset = filename[0] - BASE_VOLUME_L;
			offset = (offset >= m_mount_count) ? (m_mount_count-1) : offset;
		}
		else if ((filename[0] >= BASE_VOLUME_H) && (filename[0] < (BASE_VOLUME_H+IPANEL_MOUNTS_MAX)))
		{
			offset = filename[0] - BASE_VOLUME_H;
			offset = (offset >= m_mount_count) ? (m_mount_count-1) : offset;
		}
		/*其他情况简化成一个警告打印和指定为最后一个逻辑区域*/
		else
		{
			PORTING_DBG("warring: overflow disk index: cur = [%c], limited with [%c]~[%c] || [%c]~[%c] ...\n", filename[0], 
				BASE_VOLUME_L, (BASE_VOLUME_L+IPANEL_MOUNTS_MAX), BASE_VOLUME_H, (BASE_VOLUME_H+IPANEL_MOUNTS_MAX));
			offset = m_mount_count-1;
		}
		
		strcpy(newpath, (const char*)m_mounts[offset].mountedName);
		strcat(newpath, filename + 2);
	}
	
	/*"./" --- 当前目录 "../" ---上级目录*/
	else if (((filenameLen > 1) && (filename[0] == '/')) || ((filenameLen > 2) && (filename[0] == '.') && (filename[1] == '/')))/*./ || /*/
	{
		if (filename[0] == '.')
			offset = 1;
		strcpy(newpath, (const char*)m_mounts[0].mountedName);
		strcat(newpath, filename + offset);
	}
	else if ((filenameLen > 0) && (filename[0] != '/') && (filename[0] != '.') && (filename[1] != ':'))
	{
		strcpy(newpath, (const char*)m_mounts[0].mountedName);
		strcat(newpath, "/");
		strcat(newpath, filename);
	}
	else
	{
		PORTING_DBG("[ipanel_file_realpath]failed, filename:%s-------------fatal failed-------------\n", filename);
		return IPANEL_ERR;
	}
	
	return IPANEL_OK;
}

/*针对辽宁UI  的push_vod  路径映射*/ //ipanel_vfs_get_logic_info
/*只转换绝对路径，不转换相对路径，相对路径起始位置为应用程序所在路径*/
static INT32_T ipanel_vfs_push_vod_real_path(CHAR_T *filename, INT32_T nameLen, CHAR_T *newpath)
{
	INT32_T offset = 0;
	CHAR_T* p = filename;
	
	if ((filename == NULL) || (newpath == NULL) || (nameLen <= 0))
		return IPANEL_ERR;
	
	/*处理形如"D:"  "G:"  "F:"  目录,  ipanel  push_vod  遍历根目录时的输入既是:  "D:"*/
	if((nameLen == 2)&&(filename[1] == ':'))/*确保形如D:  ,  而非形如df*/
	{
		if ((filename[0] >= BASE_VOLUME_L) && (filename[0] < (BASE_VOLUME_L+IPANEL_MOUNTS_MAX)))
		{
			offset = filename[0] - BASE_VOLUME_L;
			if(m_mount_count == 0)
				return IPANEL_ERR;
			
			if(filename[1] == ':')
			{
				strcpy(newpath, (const char*)m_mounts[offset].mountedName);
				strcat(newpath, "/");
				
				return IPANEL_OK;
			}
		}
		else if ((filename[0] >= BASE_VOLUME_H) && (filename[0] < (BASE_VOLUME_H+IPANEL_MOUNTS_MAX)))
		{
			offset = filename[0] - BASE_VOLUME_H;
			if(m_mount_count == 0)
				return IPANEL_ERR;
			
			if(filename[1] == ':')
			{
				strcpy(newpath, (const char*)m_mounts[offset].mountedName);
				strcat(newpath, "/");
				
				return IPANEL_OK;
			}
		}
	}
	
	/*处理形如file://d:/videos/125M.TS  的路径*/
	if(nameLen >= 7)
	{
		if(strncmp("file://", filename, 7) == 0)
		{
			p = filename + 7;
		}
	}
	
	/*路径映射/  转换*/
	if ((nameLen > 2) && (p[1] == ':') && (p[2] == '/'))/*f:/*/
	{
		/*盘符是小写字母时转换成大写字母*/
		if ((p[0] >= BASE_VOLUME_L) && (p[0] < (BASE_VOLUME_L+IPANEL_MOUNTS_MAX)))
		{
			offset = p[0] - BASE_VOLUME_L;
			if(m_mount_count == 0)
				return IPANEL_ERR;
			
			offset = (offset >= m_mount_count) ? (m_mount_count-1) : offset;
		}
		
		else if ((p[0] >= BASE_VOLUME_H) && (p[0] < (BASE_VOLUME_H+IPANEL_MOUNTS_MAX)))
		{
			offset = p[0] - BASE_VOLUME_H;
			if(m_mount_count == 0)
				return IPANEL_ERR;
			
			offset = (offset >= m_mount_count) ? (m_mount_count-1) : offset;
		}
		/*其他情况简化成一个警告打印和指定为最后一个逻辑区域*/
		else
		{
			PORTING_DBG("warring: overflow disk index: cur = [%c], limited with [%c]~[%c] || [%c]~[%c] ...\n", p[0], 
				BASE_VOLUME_L, (BASE_VOLUME_L+IPANEL_MOUNTS_MAX), BASE_VOLUME_H, (BASE_VOLUME_H+IPANEL_MOUNTS_MAX));
			if(m_mount_count == 0)
				return IPANEL_ERR;
			
			offset = m_mount_count-1;
		}
		//PORTING_DBG("[ipanel_vfs_push_vod_real_path]: m_mounts[%d].mountedName = [%s]=====p[0] = [%c]\n", offset, m_mounts[offset].mountedName, p[0]);
		strcpy(newpath, (const char*)m_mounts[offset].mountedName);
		strcat(newpath, p + 2);
	}
	else
		strcpy(newpath, filename);

	return IPANEL_OK;
}

static INT32_T ipanel_vfs_add_a_dev(const CHAR_T* devName, const CHAR_T* typeStr, const CHAR_T type)
{
	INT32_T i = 0;
	IPANEL_QUEUE_MESSAGE send_msg = {0,0,0,0};
	
	if((devName == NULL) || (typeStr == NULL))
		return IPANEL_ERR;
	
	for(i = 0; i < IPANEL_DEVICES_MAX; i++)
	{
		if((m_devices[i].using == 1) && (strcmp(m_devices[i].devName, devName) ==0))
			return IPANEL_OK;
	}
	
	for(i = 0; i < IPANEL_DEVICES_MAX; i++)
	{
		if(m_devices[i].using == 0)
		{	
			strcpy(m_devices[i].devName, devName);
			strcpy(m_devices[i].typeName, typeStr);
			m_devices[i].type = type;
			m_devices[i].using = 1;
			m_devices[i].index = i;
			
			m_device_count++;
			
			PORTING_DBG("[ipanel_vfs_add_a_dev] disk: %s[%d], devices count: %d, message send to ipanel[%d]...\n", devName, i, m_device_count, EIS_DEVICE_USB_INSERT);
			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_INSERT;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);

			return IPANEL_OK;
		}
	}
	
	return IPANEL_OK;
}

/*只有两个USB  接口*/
static INT32_T ipanel_vfs_add_dev(const CHAR_T* msgStr)
{
	if(msgStr == NULL)
	{
		PORTING_DBG("[ipanel_vfs_add_device]: any NULL, bad params...\n");
		return IPANEL_ERR;
	}
	
	if(strcmp(msgStr, "add@/block/sda") == 0)
	{ 
		if(ipanel_vfs_add_a_dev("/dev/sda", "SCSI/SATA", 5) != IPANEL_OK)
			return IPANEL_ERR;
	}
	else if(strcmp(msgStr, "add@/block/sdb") == 0)
	{ 
		if(ipanel_vfs_add_a_dev("/dev/sdb", "SCSI/SATA", 5) != IPANEL_OK)
			return IPANEL_ERR;
	}
	
	return IPANEL_OK;			
}

static INT32_T ipanel_vfs_get_a_free_mnt_index(const CHAR_T* volumeName)
{
	INT32_T i = 0;
	
       PORTING_DBG("[%s:%d] volumeName=%s ------\n", __FUNCTION__, __LINE__, volumeName);
	for(i = 0; i < IPANEL_MOUNTS_MAX; i++)
	{
		if((m_mounts[i].using == 1) && (strcmp((const CHAR_T*)m_mounts[i].volumeName, volumeName) ==0))
		{
			i = -2;
			break;
		}
	}
	
	if(i == -2)
		return i;
	
       PORTING_DBG("[%s:%d]  ------\n", __FUNCTION__, __LINE__);
       
	for(i = 0; i < IPANEL_MOUNTS_MAX; i++)
	{
		if(m_mounts[i].using == 0)
		{
#if !HOT_PLUG_SOCKET
			m_freshed = 1;
#endif
			break;
		}
	}
	
       PORTING_DBG("[%s:%d] i=%d  ------\n", __FUNCTION__, __LINE__, i);

	if(i >= IPANEL_MOUNTS_MAX)
		i = -1;
	
	return i;
	
	
}
/* 
	i
	"/dev/sda" 
	"/dev/sda1"
	"/D"
*/
static INT32_T ipanel_vfs_add_a_mnt(INT32_T i, const CHAR_T* devName, const CHAR_T* volumeName, const CHAR_T* mountedName)
{
	//IPANEL_QUEUE_MESSAGE send_msg[1] = {{0, 0, 0, 0}};
	
	if((devName == NULL) || (volumeName == NULL) || (mountedName == NULL) || (i < 0) || (i >= IPANEL_MOUNTS_MAX))
		return IPANEL_ERR;
	
	if(m_mounts[i].using != 0)
		return IPANEL_ERR;
	
       PORTING_DBG("[%s:%d] i=%d------\n", __FUNCTION__, __LINE__, i);

	strcpy((CHAR_T *)m_mounts[i].devName, devName);
	strcpy((CHAR_T *)m_mounts[i].mountedName, mountedName);
	strcpy((CHAR_T *)m_mounts[i].volumeName, volumeName);
	strcpy((CHAR_T *)m_mounts[i].fileSystem, "");/*不用*/
	strcpy((CHAR_T *)m_mounts[i].modeName, "");/*不用*/
	m_mounts[i].logic = 'D' + i;
	m_mounts[i].using = 1;
	m_mounts[i].subIndex = 0;/*不用*/
	
	m_mount_count++;
	PORTING_DBG("[ipanel_vfs_add_a_mnt] mount count: %d...\n", m_mount_count);
	
	return IPANEL_OK;
}
/* "/dev/sda1" "/D" */
static INT32_T ipanel_vfs_mount(const CHAR_T* volumeName, const CHAR_T* mountedName)
{
	const static UINT32_T TIME0 = 500, LOOP = 5;
	INT32_T i = 0;
	//CHAR_T command[64];
	//CHAR_T fsType[16];
	
	if((volumeName == NULL) || (mountedName == NULL))
		return IPANEL_ERR;
	
       PORTING_DBG("[%s:%d] volumeName=%s mountedName=%s ------\n", __FUNCTION__, __LINE__, volumeName, mountedName);

	//sprintf(command, "mkdir %s", mountedName);
	//if(0 == system(command))
	//PORTING_DBG("[ipanel_vfs_mount] [%s] sucess\n",  command); 
	if(mkdir(mountedName, S_IWRITE|S_IREAD) == 0)
		PORTING_DBG("[ipanel_vfs_mount] [mkdir %s] sucess\n",  mountedName); 
	
	//sprintf(command, "mount %s %s", volumeName, mountedName);
	for (i = 0; i < LOOP; i++)
	{	
		if(m_mutex != 0)
		{
			//ipanel_porting_sem_release(m_mutex);
			ipanel_porting_task_sleep(TIME0);
			//ipanel_porting_sem_wait(m_mutex, IPANEL_WAIT_FOREVER);
		}
		//if (0 == system(command))
		//{
		//PORTING_DBG("[ipanel_vfs_mount] mount [%s] [%s] sucess\n",  volumeName, mountedName); 
		//break;
		//}
		if (mount(volumeName, mountedName, "vfat", 0, "iocharset=cp936,usefree") == 0)
		{
			PORTING_DBG("[ipanel_vfs_mount] mount -t vfat [%s] [%s] sucess\n",  volumeName, mountedName); 
			break;
		}
		else if(mount(volumeName, mountedName, "ntfs", 0, "iocharset=cp936") == 0)
		{
			PORTING_DBG("[ipanel_vfs_mount] mount -t ntfs [%s] [%s] sucess\n",  volumeName, mountedName); 
			break;
		}
	}
       PORTING_DBG("[%s:%d] i=%d LOOP=%d ------\n", __FUNCTION__, __LINE__, i, LOOP);

	if(i == LOOP)
	{
		PORTING_DBG("[ipanel_vfs_mount] mount [%s] [%s] failed\n", volumeName, mountedName); 
		return IPANEL_ERR;
	}
	
       PORTING_DBG("[%s:%d] i=%d LOOP=%d ------\n", __FUNCTION__, __LINE__, i, LOOP);

	return IPANEL_OK;
}

static INT32_T ipanel_vfs_add_mnts(const CHAR_T* msgStr)
{
	CHAR_T dev[64] = "/dev";/* "/dev/sda" */;
	CHAR_T sr[64] = "/dev";/* "/dev/sda1" */;
	CHAR_T tem[32];
	CHAR_T lg[16];
	INT32_T i = 0;
	INT32_T fd = 0;
	CHAR_T devsf[64] = "/dev";/* "/dev/sda1" */;
    
	
	if(msgStr == NULL)
	{
		PORTING_DBG("[ipanel_vfs_add_mnt]: any NULL, bad params...\n");
		return IPANEL_ERR;
	}
	PORTING_DBG("ipanel_vfs_add_mnts 1=%s \n", msgStr);
#if HOT_PLUG_SOCKET
	/* msg like "add@/block/sda/sda1" */
	if(strncmp("add@/block", msgStr, 10) != 0)
	{
		PORTING_DBG("[ipanel_vfs_add_mnt]: no match %s\n", msgStr);
		return IPANEL_ERR;
	}
       PORTING_DBG("[%s:%d] msgLen=%d------\n", __FUNCTION__, __LINE__, strlen(msgStr));
	
	/*消除形如"add@/block/sda"  的消息*/
	if(strlen(msgStr) < 14)
		return IPANEL_OK;
	else if(strlen(msgStr) == 14)
    {
    	memset(tem,0x00,sizeof(tem));
    	strncpy(tem, msgStr+10, 4);/*"/sda"*/
    	tem[4] = '\0';
    	sprintf(dev, "%s%s", dev, tem);/* "/dev/sda" */
        PORTING_DBG("[%s:%d] msgLen=%d------\n", __FUNCTION__, __LINE__, strlen(msgStr));
    	sprintf(sr, "%s%s", sr, tem);/* "/dev/sda" */
        PORTING_DBG("[%s:%d] sr=%s------\n", __FUNCTION__, __LINE__,  (sr));
        memset(devsf, 0x00, sizeof(devsf));
    	sprintf(devsf, "%s1", sr);/* "/dev/sda1" */
		ipanel_porting_task_sleep(50);
        if((fd=open(devsf, O_RDONLY)) == -1)
        {
           PORTING_DBG("[%s:%d] not exit sda1 ; ok continue fd=%d devsf=%s ------\n", __FUNCTION__, __LINE__,  (fd), devsf);
        }
        else
        {
           PORTING_DBG("[%s:%d]   exit sda1 ; error return fd=%d devsf=%s------\n", __FUNCTION__, __LINE__,  (fd), devsf);
           close(fd);
		   return IPANEL_ERR;
        }
    }
    else
    {
    	strncpy(tem, msgStr+10, 4);/*"/sda"*/
    	tem[4] = 0;
    	sprintf(dev, "%s%s", dev, tem);/* "/dev/sda" */
		//add@/block/sda/sda1
    	strncpy(tem, msgStr+14, 5);/*"/sda1"*/
    	tem[5] = 0;
    	sprintf(sr, "%s%s", sr, tem);/* "/dev/sda1" */
        PORTING_DBG("[%s:%d] sr=%s------\n", __FUNCTION__, __LINE__,  (sr));
     }
#else
	sprintf(sr, "%s", msgStr);
	strncpy(dev, msgStr, 8);
#endif


	i = ipanel_vfs_get_a_free_mnt_index(sr);

       PORTING_DBG("[%s:%d] i=%d------\n", __FUNCTION__, __LINE__, i);
       
	if((i == -1) || (i > IPANEL_MOUNTS_MAX))
		return IPANEL_ERR;
	if(i == -2)
		return IPANEL_OK;
	
	lg[0] = '/';
	lg[1] = 'D' + i;
	lg[2] = '\0';

//	memcpy(lg,"本地播放",8);
	//lg[8] = '\0';

	
       PORTING_DBG("[%s:%d] i=%d------\n", __FUNCTION__, __LINE__, i);
	
	if(ipanel_vfs_mount(sr, lg) != IPANEL_OK)
		return IPANEL_ERR;
	
       PORTING_DBG("[%s:%d] i=%d------\n", __FUNCTION__, __LINE__, i);

	if(ipanel_vfs_add_a_mnt(i, dev, sr, lg) != IPANEL_OK)
		return IPANEL_ERR;

	return IPANEL_OK;
}

static INT32_T ipanel_vfs_del_dev(const CHAR_T* msgStr)
{
	INT32_T i = 0;
	IPANEL_QUEUE_MESSAGE send_msg = {0, 0, 0, 0};
	
	if(msgStr == NULL)
	{
		PORTING_DBG("[ipanel_vfs_add_mnt]: any NULL, bad params...\n");
		return IPANEL_ERR;
	}
	
	if(strncmp("remove@/block/sda", msgStr,17) == 0)
	{
		for(i = 0; i < IPANEL_DEVICES_MAX; i++)
		{
			/*匹配设备名称和状态检测*/
			if((m_devices[i].using == 1) && (strcmp("/dev/sda", m_devices[i].devName) == 0))
			{
				memset(&m_devices[i], 0x00, sizeof(IPANEL_DEVICE_INFO_t));
				m_device_count--;
				PORTING_DBG("[ipanel_vfs_del_dev] devices count: %d, message send to ipanel[%d]...\n", m_device_count, EIS_DEVICE_USB_DELETE);

				send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
				send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_DELETE;
				send_msg.q3rdWordOfMsg = 0;
				porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);

				send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_UNAVAILABLE;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);	

		//	send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_NETWORK;
		//	send_msg.q2ndWordOfMsg = EIS_IP_NETWORK_READY;
	//		send_msg.q3rdWordOfMsg = 0;
	//		porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);	
				

				break;
			}
		}
		if(i == IPANEL_DEVICES_MAX)
			return IPANEL_ERR;
		
	}
	else if(strcmp("remove@/block/sdb", msgStr) == 0)
	{
		for(i = 0; i < IPANEL_DEVICES_MAX; i++)
		{
			/*匹配设备名称和状态检测*/
			if((m_devices[i].using == 1) && (strcmp("/dev/sdb", m_devices[i].devName) == 0))
			{
				memset(&m_devices[i], 0x00, sizeof(IPANEL_DEVICE_INFO_t));
				m_device_count--;
				PORTING_DBG("[ipanel_vfs_del_dev] devices count: %d\n", m_device_count);
				//PORTING_DBG("[ipanel_vfs_del_dev] devices count: %d, message send to ipanel[%d]...\n", m_device_count, IPANEL_DEVICE_USB_DELETE);
				
				//send_msg->q1stWordOfMsg = IPANEL_EVENT_TYPE_IPTV;
				//send_msg->q2ndWordOfMsg = IPANEL_DEVICE_USB_DELETE;
				//ipanel_porting_queue_send(g_sys_gbl.msg_queue_handle, send_msg);
				send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
				send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_DELETE;
				send_msg.q3rdWordOfMsg = 0;
				porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);
				send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_UNAVAILABLE;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);	
			/*
			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_NETWORK;
			send_msg.q2ndWordOfMsg = EIS_IP_NETWORK_READY;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);	
			*/
				break;
			}
		}
		if(i == IPANEL_DEVICES_MAX)
			return IPANEL_ERR;
		
	}
	
	
	return IPANEL_OK;
}

static INT32_T ipanel_vfs_del_a_dev(const CHAR_T* msgStr)
{
	INT32_T i = 0;
	IPANEL_QUEUE_MESSAGE send_msg = {0, 0, 0, 0};
	
	if(msgStr == NULL)
	{
		PORTING_DBG("[ipanel_vfs_add_mnt]: any NULL, bad params...\n");
		return IPANEL_ERR;
	}
	
	for(i = 0; i < IPANEL_DEVICES_MAX; i++)
	{
		/*匹配设备名称和状态检测*/
		if((m_devices[i].using == 1) && (strcmp(msgStr, m_devices[i].devName) == 0))
		{
			memset(&m_devices[i], 0x00, sizeof(IPANEL_DEVICE_INFO_t));
			m_device_count--;
			PORTING_DBG("[ipanel_vfs_del_dev] devices count: %d, message send to ipanel[%d]...\n", m_device_count, EIS_DEVICE_USB_DELETE);
			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_DELETE;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);

			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_INSTALL;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);	
/*
			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_NETWORK;
			send_msg.q2ndWordOfMsg = EIS_IP_NETWORK_READY;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);	
			*/
			break;
		}
	}
	if(i == IPANEL_DEVICES_MAX)
		return IPANEL_ERR;
	
	return IPANEL_OK;
}
/*"/D"*/
static INT32_T ipanel_vfs_umount(const CHAR_T* mountedName)
{
	const static UINT32_T TIME0 = 1000, LOOP = 10;
	INT32_T i = 0;
	//CHAR_T command[64];
	
	if(mountedName == NULL)
		return IPANEL_ERR;
	
	//sprintf(command, "umount -l %s", mountedName);
	for (i = 0; i < LOOP; i++)
	{
		//if (0 == system(command))
		if(umount(mountedName) == 0)
		{
			PORTING_DBG("[ipanel_vfs_umount] umount %s sucess\n", mountedName); 
			//PORTING_DBG("[ipanel_vfs_umount] %s sucess\n", command); 
			//sprintf(command, "rmdir %s", mountedName);
			//if(0 == system(command))
			//PORTING_DBG("[ipanel_vfs_umount] %s sucess\n", command); 
			if(rmdir(mountedName) == 0)
				PORTING_DBG("[ipanel_vfs_umount] rmdir %s sucess\n", mountedName); 
			break;
		}
		ipanel_porting_task_sleep(TIME0);
	}
	if(i == LOOP)
	{
		PORTING_DBG("[ipanel_vfs_umount] umount %s failed\n", mountedName); 
		if(rmdir(mountedName) == 0)
			PORTING_DBG("[ipanel_vfs_umount] rmdir %s sucess\n", mountedName); 
		return IPANEL_ERR;
	}
	
	return IPANEL_OK;	
}

static INT32_T ipanel_vfs_get_index_by_volume(const CHAR_T* volumeName)
{
	INT32_T i = 0;
	
	for(i = 0; i < IPANEL_MOUNTS_MAX; i++)
	{
		if((m_mounts[i].using == 1) && strcmp((const CHAR_T *)m_mounts[i].volumeName, volumeName) ==0)
			break;
	}
	
	if(i >= IPANEL_MOUNTS_MAX)
		return -1;
	
	return i;
}

static INT32_T ipanel_vfs_del_a_mnt(const CHAR_T* mountedName)
{
	INT32_T i = 0;
	//IPANEL_QUEUE_MESSAGE send_msg[1] = {{0, 0, 0, 0}};
	
	if(mountedName == NULL)
	{
		PORTING_DBG("[ipanel_vfs_add_mnt]: any NULL, bad params...\n");
		return IPANEL_ERR;
	}
	
	for(i = 0; i < IPANEL_MOUNTS_MAX; i++)
	{
		/*匹配设备名称*/
		if(strcmp((const CHAR_T *)m_mounts[i].mountedName, mountedName) == 0)
		{
			memset(&m_mounts[i], 0x00, sizeof(IPANEL_LOGIC_INFO_t));
			m_mount_count--;
			
			PORTING_DBG("[ipanel_vfs_del_a_mnt] mount count: %d...\n", m_mount_count);
			break;
		}
	}
	if(i == IPANEL_MOUNTS_MAX)
		return IPANEL_ERR;
	
	return IPANEL_OK;
}

static INT32_T ipanel_vfs_del_mnts(const CHAR_T* msgStr)
{
	CHAR_T sr[64] = "/dev";/* "/dev/sda1" */;
	CHAR_T tem[32];
	CHAR_T lg[16];
	INT32_T i = 0;
	
	if(msgStr == NULL)
	{
		PORTING_DBG("[ipanel_vfs_del_mnts]: any NULL, bad params...\n");
		return IPANEL_ERR;
	}
	
#if HOT_PLUG_SOCKET
	/* msg like "remove@/block/sda/sda1" */
	if(strncmp("remove@/block", msgStr, 10) != 0)
	{
		PORTING_DBG("[ipanel_vfs_del_mnts]: no match %s\n", msgStr);
		return IPANEL_ERR;
	}

      PORTING_DBG("[%s%d] msg=%s\n", __FUNCTION__, __LINE__, msgStr);
	/*消除形如"add@/block/sda"  的消息*/
	if(strlen(msgStr) < 17)
		return IPANEL_OK;

	if(strlen(msgStr) == 17)
    {
    	strncpy(tem, msgStr+13, 4);/*"/sda"*/
    	tem[4] = 0;
    	sprintf(sr, "%s%s", sr, tem);/*"/dev/sda"*/
        PORTING_DBG("[%s:%d] sr=%s------\n", __FUNCTION__, __LINE__,  (sr));
    }
    else
    {
    	strncpy(tem, msgStr+17, 5);/*"/sda1"*/
    	tem[5] = 0;
    	sprintf(sr, "%s%s", sr, tem);/*"/dev/sda1"*/
        PORTING_DBG("[%s:%d] sr=%s------\n", __FUNCTION__, __LINE__,  (sr));
    }
#else
	sprintf(sr, "%s", msgStr);
#endif
	
      PORTING_DBG("[%s%d] sr=%s\n", __FUNCTION__, __LINE__, sr);

	i = ipanel_vfs_get_index_by_volume(sr);
	if(i < 0)
		return IPANEL_ERR;
	
	lg[0] = '/';
	lg[1] = 'D' + i;
	lg[2] = '\0';

      PORTING_DBG("[%s%d] lg=%s\n", __FUNCTION__, __LINE__, lg);
	
#if HOT_PLUG_SOCKET	
	
	ipanel_vfs_umount(lg);
	if(ipanel_vfs_del_a_mnt(lg) != IPANEL_OK)
		return IPANEL_ERR;
#else
	if(ipanel_vfs_del_a_mnt(lg) != IPANEL_OK)
		return IPANEL_ERR;
	
	if(ipanel_vfs_umount(lg) != IPANEL_OK)
		return IPANEL_ERR;
#endif	
		
	return IPANEL_OK;
}


/*获得一个逻辑设备的信息*/
static INT32_T ipanel_vfs_get_logic_info(INT32_T devIndex, INT32_T logIndex, IPANEL_STORAGE_DEV_INFO* info)
{
	INT32_T i = 0, j = 0;
	
	if(info == NULL)
		return IPANEL_ERR;
	
	if(devIndex > 1)
		return IPANEL_ERR;
	
	for(i = 0; i < IPANEL_MOUNTS_MAX; i++)
	{
		if(m_mounts[i].using != 1)
			continue;
		
		if(strcmp((const CHAR_T *)m_mounts[i].devName, (const CHAR_T *)m_devices[devIndex].devName) != 0)
			continue;
		
		if((j+1) == logIndex)
		{
			struct statfs st; 
			
			if(0 != statfs((const CHAR_T *)m_mounts[i].mountedName, &st))
				continue;
			
			PORTING_DBG("[ipanel_vfs_get_logic_info]: [%d]-[%d]-[0x%x]-[%d]-[%s]\n", st.f_bsize, st.f_blocks, st.f_bfree, st.f_type, m_mounts[i].mountedName);
			info->start = 0;
			info->size = st.f_bsize/1024 * st.f_blocks;
			info->free = st.f_bsize/1024 * st.f_bfree;
			info->logic = m_mounts[i].logic;
			info->type = st.f_type;
	
			info->type = 1;

			strcpy(info->name, (const CHAR_T *)m_mounts[i].mountedName);	
			//strcpy(info->name, (const char*)"本地磁盘");

			//printf("[ipanel_vfs_get_logic_info]: [%d]-[%d]-[%d]-[%c]-[%d]-[%s]\n", info->start, info->size, info->free, info->logic, info->type, info->name);
			
			return IPANEL_OK;
		}
		else
			j++;
	}

	return IPANEL_OK;
}

/*获得一个硬件存储设备的信息*/
static INT32_T ipanel_vfs_get_a_dev_info(INT32_T devIndex, IPANEL_STORAGE_DEV_INFO* info)
{
	INT32_T i = 0;
	
	if(info == NULL)
		return IPANEL_ERR;
	
	info->free = 0;
	info->size = 0;
	
	if(devIndex > 1)
		return IPANEL_ERR;
	
	for(i = 0; i < IPANEL_MOUNTS_MAX; i++)
	{
		struct statfs st; 
		
		if(m_mounts[i].using != 1)
			continue;
		
		if(strcmp((const CHAR_T *)m_mounts[i].devName, (const CHAR_T *)m_devices[devIndex].devName) != 0)
			continue;
		
		if(0 != statfs((const CHAR_T *)m_mounts[i].mountedName, &st))
			continue;
		
		info->size += st.f_bsize/1024 * st.f_blocks;
		info->free += st.f_bsize/1024 * st.f_bfree;
	}
	PORTING_DBG("[ipanel_vfs_get_a_dev_info]: [%d]-[%d]\n", info->size, info->free);
	
	return IPANEL_OK;
}

static void ipanel_vfs_trave_usb_storage_devices()
{
	CHAR_T disk[32] = "/dev/sd";
	CHAR_T volume[32] = "dev/sd";
	CHAR_T t[3];
	INT32_T i = 0, j = 0;
	//INT32_T isAdd  = 0;
	IPANEL_QUEUE_MESSAGE send_msg = {0, 0, 0, 0};
	
	for(i = 0; i < IPANEL_DEVICES_MAX; i++)
	{
		t[0] = 'a' + i;
		t[1] = '\0';
		sprintf(disk, "/dev/sd%s", t);
		
		if(access(disk, R_OK) == 0)
		{
		/*if(ipanel_vfs_add_a_dev(disk, "SCSI/SATA", 5) != IPANEL_OK)
		continue;
		else
			isAdd = 1;*/
			ipanel_vfs_add_a_dev(disk, "SCSI/SATA", 5);	
		}
		else
		{
		/*if(ipanel_vfs_del_a_dev(disk) != IPANEL_OK)
		continue;
		else
			isAdd = 0;*/
			ipanel_vfs_del_a_dev(disk);	
		}
		
		for(j = 0; j < IPANEL_MAX_LOGICS_PER_DEV; j++)
		{
			t[0] = 'a' + i;
                    if(j==0)
                        t[1] = '\0';
                    else
        			t[1] = '0' + j;
			t[2] = '\0';
			sprintf(volume, "/dev/sd%s", t);
			if(access(volume, R_OK) == 0)
			{
				//if(isAdd == 0)
				//continue;
				ipanel_vfs_add_mnts(volume);
			}
			else
			{
				//if(isAdd == 1)
				//continue;
				ipanel_vfs_del_mnts(volume);
			}
		}
	}
	
	if(m_freshed == 1)
	{
		PORTING_DBG("[ipanel_vfs_trave_usb_storage_devices] mount count: %d, message send to ipanel[%d]...\n", m_mount_count, EIS_DEVICE_USB_INSTALL);
		
		/*向中间件发送消息*/

		PORTING_DBG("[ipanel_vfs_trave_usb_storage_devices] start_time = %d\n",ipanel_porting_time_ms());
		send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
		send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_INSTALL;
		porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);
	//	ipanel_porting_queue_send(g_sys_gbl.msg_queue_handle, send_msg);
		m_freshed = 0;
	}
}

/*=======================================================================*/
static INT32_T ipanel_vfs_init_hotplug_sock() 
{ 
    //const INT32_T buffersize = 1024; 
  //  INT32_T ret; 
	#define UEVENT_BUFFER_SIZE 2048  
  const int buffersize = 1024;  
  int ret;  

  struct sockaddr_nl snl;  
  bzero(&snl, sizeof(struct sockaddr_nl));  
  snl.nl_family = AF_NETLINK;  
  snl.nl_pid = getpid();  
  snl.nl_groups = 1;  

  int s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);  
  if (s == -1)   
  {  
  perror("socket");  
  return -1;  
  }  
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, &buffersize, sizeof(buffersize));  

  ret = bind(s, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl));  
  if (ret < 0)   
  {  
  perror("bind");  
  close(s);  
  return -1;  
  }  

  return s;  
} 
#if 0
    struct sockaddr_nl snl; 
    bzero(&snl, sizeof(struct sockaddr_nl)); 
    snl.nl_family = AF_NETLINK; 
    snl.nl_pid = getpid(); 
    snl.nl_groups = 1; 
	
    INT32_T s = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT); 
    if (s == -1)  
    { 
        perror("socket"); 
        return -1; 
    } 
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &buffersize, sizeof(buffersize)); 
	
    ret = bind(s, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl)); 
    if (ret < 0)  
    { 
        perror("bind"); 
        close(s); 
        return -1; 
    } 
	
    return s; 
}
#endif
static INT32_T ipanel_vfs_wait_for(INT32_T fd,   INT32_T   timeoutms)   
{   
	fd_set   rfds;   
	struct   timeval   tv   =   {0,   timeoutms};   
	FD_ZERO(&rfds);   
	FD_SET(fd,   &rfds);   
	return  select(fd   +   1,   &rfds,   NULL,   NULL,  &tv);   
}
static INT32_T Init_UsbInsert()
{
	#define LINE_BUFFER_SIZE 		256

	typedef struct{
		BYTE_T dev[IPANEL_USB_NAME_LEN_MAX];
		BYTE_T part_cnt;
		BYTE_T parts[IPANEL_MOUNTS_MAX][IPANEL_USB_NAME_LEN_MAX];
	}REMOVE_DISK;
	BYTE_T i,j;
	FILE* pf;
	BYTE_T *p;		
	IPANEL_QUEUE_MESSAGE send_msg;
	REMOVE_DISK devs[IPANEL_DEVICES_MAX];
	BYTE_T dev_files[IPANEL_DEVICES_MAX][IPANEL_USB_NAME_LEN_MAX];	
	BYTE_T buf[LINE_BUFFER_SIZE];

	pf = fopen("/proc/partitions", "rb"); 
	if (NULL==pf)
	{
		printf("cannot open partitions\n");
		return;
	}
	memset(devs,0,sizeof(REMOVE_DISK)*IPANEL_DEVICES_MAX);
	for(i=0;i<IPANEL_DEVICES_MAX;i++)
	{
		strcpy(dev_files[i],"sda\xa");
		dev_files[i][2]+=i; 
	}	
	while(NULL!=fgets(buf,LINE_BUFFER_SIZE,pf))
	{
		p=strstr(buf,"sd");		
		if(NULL!=p)
		{
			printf("[YNL@%s(%d){%s}]\n",__FUNCTION__,__LINE__,p);
			for(i=0;i<IPANEL_DEVICES_MAX;i++)
			{
				if(0==memcmp(p,dev_files[i],3))
				{
					if(0==strcmp(p,dev_files[i]))
	{
						strcpy(devs[i].dev,p);
					}
					else
					{
						strcpy(devs[i].parts[devs[i].part_cnt++],p);	
					}					
					break;
				}
			}
		}
	}

	for(i=0;i<IPANEL_DEVICES_MAX;i++)
	{
		//dev exist
		if(0!=devs[i].dev[0])
		{
			strcpy(buf,"add@/block/");
			devs[i].dev[3]=0;
			strcat(buf,devs[i].dev);
			ipanel_vfs_add_dev(buf);
			//printf("add dev(%s)\n",buf);
			if(0==devs[i].part_cnt)
			{
				ipanel_vfs_add_mnts(buf);
		}	
			else
			{
				for(j=0;j<devs[i].part_cnt;j++)
		{
					strcpy(buf,"add@/block/");
					devs[i].dev[3]=0;
					strcat(buf,devs[i].dev);	
					strcat(buf,"/");			
					devs[i].parts[j][4]=0;
					strcat(buf,devs[i].parts[j]);	
					ipanel_vfs_add_mnts(buf);
				}
			}
			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_INSTALL;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);
		}
	}
	usbflag = 1;
	fclose(pf); 
}

static INT32_T ipanel_vfs_task() 
{	
	INT32_T hotplug_sock = ipanel_vfs_init_hotplug_sock(); 
	IPANEL_QUEUE_MESSAGE send_msg ={0};
	
	while(1) 
	{ 
		char buf[UEVENT_BUFFER_SIZE * 2] = {0}; 
		buf[0] = '\0';
		
		if(ipanel_vfs_wait_for(hotplug_sock, 0) == 1)
		{
			recv(hotplug_sock, &buf, sizeof(buf), 0);
		}
		else
		{
			if (usbflag == 0)
			{
				Init_UsbInsert();
			}
			ipanel_porting_task_sleep(10);
			continue;
		}

		if(buf[0] != 0)
			PORTING_DBG("[ipanel_vfs_task]: hotplug msg is [%s]\n", buf); 
	
		/*添加*/
		if(strncmp(buf, "add@/block", 10) == 0)
		{
			/*添加物理设备*/
			ipanel_vfs_add_dev(buf);
			
			/*添加逻辑设备*/
			ipanel_vfs_add_mnts(buf);
		}
		else if(strncmp(buf, "remove@/block", 13) == 0)
		{
			/*删除逻辑设备*/
			ipanel_vfs_del_mnts(buf);
			/*删除硬盘设备*/
			ipanel_vfs_del_dev(buf);
		}
		/*收到这个消息,  就认为该usb  的卷已经mount  完毕,  这种方式只能支持单usb  ,  
		对于多usb,  依然会出现多次发送install  的消息,  导致ipanel  库中多次建立同一个
		mount点的问题*/
		
		if(strncmp(buf, "add@/class/scsi_device", 22) == 0 /*|| strncmp(buf, "remove@/class/scsi_device", 25) == 0*/)
		{
			PORTING_DBG("[ipanel_vfs_task] message send to ipanel[%d]...\n", EIS_DEVICE_USB_INSTALL);
			send_msg.q1stWordOfMsg = EIS_EVENT_TYPE_IPTV;
			send_msg.q2ndWordOfMsg = EIS_DEVICE_USB_INSTALL;
			send_msg.q3rdWordOfMsg = 0;
			porting_event_put(PORT_EVENT_TYPE_SYSTEM, (PORT_EVENT_T *)&send_msg);
		}
	//	ipanel_porting_sem_release(m_mutex);
	
	} 

	/*线程安全退出*/
	m_exit = 1;
	return IPANEL_OK;
}
/*=======================================================================*/


#define TEST_IPANEL_VFS
#ifdef TEST_IPANEL_VFS
void detectUSBDevice()
{
		printf("malloc 77\n");
	char *pBuf = malloc(512);
	void  *pfile = NULL;
	char *p = NULL;
	INT32_T file_size;
	INT32_T ret = 0;

	pfile = fopen("/proc/partitions", "rb");
	if (pfile == 0)
	{
		free(pBuf);
		printf("cannot open partitions\n");
		return;
	}
	file_size = fread(pBuf, 1, 512, pfile);
	if (file_size == 0)
	{
		free(pBuf);
		fclose(pfile);
		printf("cannot read partitions\n");
		return;
	}
	if (strstr(pBuf, "sda1") != NULL)
	{
		pri_port_syscall(MSG_SYSCALL,"mount /dev/sda1 /mnt/block1");
	}
	else if (strstr(pBuf, "sda") != NULL)
	{
		pri_port_syscall(MSG_SYSCALL,"mount /dev/sda /mnt/block1");
	}
		
	if (strstr(pBuf, "sdb1") != NULL)
	{
		pri_port_syscall(MSG_SYSCALL, "mount /dev/sdb1 /mnt/block2");
	}
	else if (strstr(pBuf, "sdb") != NULL)
	{
		pri_port_syscall(MSG_SYSCALL, "mount /dev/sdb /mnt/block2");
	}
	free(pBuf);
	fclose(pfile);
}
int test_usb_dev_proc() 
{ 
	INT32_T i;
	INT32_T hotplug_sock = ipanel_vfs_init_hotplug_sock(); 
	char command[32];
	INT32_T ret;
	detectUSBDevice();
	while(1) 
	{ 
		/* Netlink message buffer */ 
		char buf[UEVENT_BUFFER_SIZE * 2] = {0}; 
		recv(hotplug_sock, &buf, sizeof(buf), 0); 
		PORTING_DBG("[test_usb_dev_proc] [%s]\n", buf); 

		if(strcmp("add@/block/sda", buf) == 0) //添加第一块usb
		{
			memset(command, 0, sizeof(command));
			strcpy(command, "mount /dev/sda /mnt/block1");
		}
		else if(strcmp("add@/block/sda/sda1", buf) == 0) //添加第一块usb
		{
			memset(command, 0, sizeof(command));
			strcpy(command, "mount /dev/sda1 /mnt/block1");
		}
		else if (strcmp("add@/block/sdb", buf) == 0) //添加第二块usb
		{
			memset(command, 0, sizeof(command));
			strcpy(command, "mount /dev/sdb /mnt/block2");
		}
		else if (strcmp("add@/block/sdb/sdb1", buf) == 0) //添加第二块usb
		{
			memset(command, 0, sizeof(command));
			strcpy(command, "mount /dev/sdb1 /mnt/block2");
		}
		else if (strcmp("remove@/block/sda", buf) == 0) //删除第一块usb
		{
			memset(command, 0, sizeof(command));
			strcpy(command, "umount /mnt/block1");
			printf("%s \n",command);
			
			//pri_port_syscall(MSG_SYSCALL,command);
			printf("system = %d\n",cy_system(command));
		}
		else if (strcmp("remove@/block/sdb", buf) == 0) //删除第二块usb
		{
			memset(command, 0, sizeof(command));
			strcpy(command, "umount /mnt/block2");
			printf("%s \n",command);
			
			//pri_port_syscall(MSG_SYSCALL,command);
			printf("system = %d\n",cy_system(command));
		}
		else if (strncmp("add@/class/scsi_device", buf,22) == 0)
		{
			printf("%s \n",command);

			//做一次保护，不知道有没有必要,add by fangdi
			//pri_port_syscall(MSG_SYSCALL,"umount /mnt/block1");
			//pri_port_syscall(MSG_SYSCALL,"umount /mnt/block2");
			//pri_port_syscall(MSG_SYSCALL,"ls");
			//pri_port_syscall(MSG_SYSCALL,"whoami");
			//pri_port_syscall(MSG_SYSCALL,"ls");
			//pri_port_syscall(MSG_SYSCALL,"ls /mnt");
			//pri_port_syscall(MSG_SYSCALL,"ls /dev");
			//pri_port_syscall(MSG_SYSCALL,command);
			cy_system("cat /proc/partitions");
			ret = cy_system(command);
			printf("system 111= %d error = %s\n",ret, strerror(ret));
			cy_system("ls /mnt/block1");
			cy_system("ls /mnt/block2");
		}
		
	} 
	
	return 0; 
}

INT32_T ipanel_test_vfs(VOID)
{
	UINT32_T 	pFile;
	char 	   *teststr 	= NULL;
	BYTE_T 	   *test_buffer = NULL;
	INT32_T 	nbytes 		= 0;
	INT32_T		len 		= 0;
	INT32_T		nRet 		= IPANEL_ERR;
	INT32_T		volume_cnt 	= 0;
	
	nRet = ipanel_vfs_init();
	if(nRet != IPANEL_OK)
	{
		PORTING_DBG("[ipanel_test_vfs] ipanel_file_init() failed!\n");
		return IPANEL_ERR;
	}
	
	while(1)
	{
		ipanel_porting_task_sleep(10);
		continue;
		
		IPANEL_QUEUE_MESSAGE msg;
		
		INT32_T r = ipanel_porting_queue_recv(g_sys_gbl.msg_queue_handle, &msg, IPANEL_NO_WAIT);
		if (r == IPANEL_OK)
		{
			switch(msg.q1stWordOfMsg)
			{
				
			case EIS_EVENT_TYPE_IPTV:
				{
					if(msg.q2ndWordOfMsg == EIS_DEVICE_USB_INSERT)	
					{
						//ipanel_vfs_list_devs();
					}
					
					else if(msg.q2ndWordOfMsg == EIS_DEVICE_USB_INSTALL)	
					{
						INT32_T n0 = 0, n1 = 0;
						//ipanel_vfs_list_mnts();
						//n0 = ipanel_vfs_get_logics_count(0);
						//n1 = ipanel_vfs_get_logics_count(1);
						PORTING_DBG("[ipanel_test_vfs] 0:[%d], 1[%d]\n", n0, n1);
					}
					
					else if(msg.q2ndWordOfMsg == EIS_DEVICE_USB_DELETE)	
					{
						//ipanel_vfs_list_devs();
						//ipanel_vfs_list_mnts();
					}
				}
				break;
				
			default:
				break;
				
			}
		}
		
		ipanel_porting_task_sleep(10);
		
	}
	
	ipanel_vfs_exit();
	
	return IPANEL_OK;
}
#endif //TEST_IPANEL_VFS

