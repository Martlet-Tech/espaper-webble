#ifndef IST9201_H
#define IST9201_H

typedef struct {
	unsigned char (*setPmic)(void);
	unsigned char (*sendPmicData)(unsigned char *dataBuffer,
				      unsigned int dataLength);
	void (*DelayMs)(unsigned int delaytime);

	void (*DetectHWVersion)(void);
	void (*IfInit)(void);
	void (*IfDeinit)(void);
	void (*PowerOnPMIC)(void);
	void (*PowerOffPMIC)(void);
	void (*enablePmic)(void);
	void (*disablePmic)(void);
	void (*powerSwitchEnable)(void);
	void (*powerSwitchDisable)(void);
	unsigned int (*voltageToRegisterData)(unsigned int voltageData,
					      unsigned char setSelect);

	//int pmic_en_pin;
	//int ist_en_pin;
	//int vddp_en_pin;
	//int vddn_en_pin;
	//int vncp_en_pin;
	//int sda_pin;
	//int scl_pin;
	//int pg_pin;
	//int ps_pin;
} IST9201;

extern IST9201 ist9201;

typedef void *ist9201_handle_t;
ist9201_handle_t ist9201_create(int pmic_en, int ist_en, int vddp_en,
				int vddn_en, int vncp_en, int sda_pin,
				int scl_pin, int pg_pin, int ps_pin);

void ist9201_detectHardwareVersion(ist9201_handle_t handle);
void ist9201_interfaceInit(ist9201_handle_t handle);
void ist9201_interfaceDeinit(ist9201_handle_t handle);
void ist9201_enablePmic(ist9201_handle_t handle);
void ist9201_disablePmic(ist9201_handle_t handle);
void ist9201_powerSwitchEnable(ist9201_handle_t handle);
void ist9201_powerSwitchDisable(ist9201_handle_t handle);
unsigned int ist9201_voltageToRegisterData(ist9201_handle_t handle,
					   unsigned int voltageData,
					   unsigned char setSelect);

#ifdef __cplusplus
}
#endif

#endif //__IST9201
