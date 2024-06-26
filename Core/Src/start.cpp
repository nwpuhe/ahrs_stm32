#include "start.h"
#include "main.h"
#include "usart.h"
#include "icm20948.h"
#include <string>
using namespace std;

extern uint8_t time1000ms, time5ms;

// for stm32cube monitor debug
float debug[20] = {0};

extern uint8_t imuDataBuffer[12];

extern int16_t Accel_X_RAW;
extern int16_t Accel_Y_RAW;
extern int16_t Accel_Z_RAW;

extern int16_t Gyro_X_RAW;
extern int16_t Gyro_Y_RAW;
extern int16_t Gyro_Z_RAW;

extern int16_t Magn_X_RAW;
extern int16_t Magn_Y_RAW;
extern int16_t Magn_Z_RAW;

extern float Ax, Ay, Az, Gx, Gy, Gz;

void start_up()
{
	ICM20948_Init();

	while (1)
	{
		if (time1000ms)
		{
			string str = "Powered By QizhiHe, Wechat: 1210106584\r\n";
			HAL_UART_Transmit(&huart1, (uint8_t *)str.data(), str.size(), 0xff);
			HAL_GPIO_TogglePin(LED_STATE_GPIO_Port, LED_STATE_Pin);

			time1000ms = 0;
		}

		if (time5ms)
		{
			ICM20948_Read_Gyro();
			ICM20948_Read_Accel();
			// ICM20948_Read_Magn_Polling();

			{	
				debug[0] = Magn_X_RAW;
				debug[1] = Magn_Y_RAW;
				debug[2] = Magn_Z_RAW;

				debug[3] = Gyro_X_RAW;
				debug[4] = Gyro_Y_RAW;
				debug[5] = Gyro_Z_RAW;

				// debug[6] = Accel_X_RAW;
				// debug[7] = Accel_Y_RAW;
				// debug[8] = Accel_Z_RAW;

				// debug[9] = Gx;
				// debug[10] = Gy;
				// debug[11] = Gz;

				// debug[12] = Ax;
				// debug[13] = Ay;
				// debug[14] = Az;

				// debug[15] = Gx;
				// debug[16] = Gy;
				// debug[17] = Gz;
			}

			time5ms = 0;
		}
	}
}
