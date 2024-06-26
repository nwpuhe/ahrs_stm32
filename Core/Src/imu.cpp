#include <cmath>
#include "imu.h"
#include "main.h"
#include "i2c.h"
#include "embedMath.h"

using namespace matrix;

#define MAG_DATA_LEN 6
// #define Kp 4.50f // proportional gain governs rate of convergence to accelerometer/magnetometer
// #define Ki 1.00f // integral gain governs rate of convergence of gyroscope biases

#define Kp 0.00f // proportional gain governs rate of convergence to accelerometer/magnetometer
#define Ki 0.00f // integral gain governs rate of convergence of gyroscope biases

#define rad2deg (180.0f / M_PI)
#define deg2rad (M_PI / 180.0f)

#define dig_T1 bmp280.T1
#define dig_T2 bmp280.T2
#define dig_T3 bmp280.T3
#define dig_P1 bmp280.P1
#define dig_P2 bmp280.P2
#define dig_P3 bmp280.P3
#define dig_P4 bmp280.P4
#define dig_P5 bmp280.P5
#define dig_P6 bmp280.P6
#define dig_P7 bmp280.P7
#define dig_P8 bmp280.P8
#define dig_P9 bmp280.P9
#define t_fine bmp280.T_fine
#define MSLP 101325 // Mean Sea Level Pressure = 1013.25 hPA (1hPa = 100Pa = 1mbar)

float angles[3];
float q0, q1, q2, q3;
IMU_ST_SENSOR_DATA gstGyroOffset = {0, 0, 0};
BMP280_HandleTypeDef bmp280;
int32_t gs32Pressure0 = MSLP;

ICM20948::ICM20948()
{
    _attInitialized = 0;
}

// public
void ICM20948::imuInit(IMU_EN_SENSOR_TYPE *penMotionSensorType, IMU_EN_SENSOR_TYPE *penPressureType)
{
    bool bRet = false;

    bRet = icm20948Check();
    if (true == bRet)
    {
        *penMotionSensorType = IMU_EN_SENSOR_TYPE_ICM20948;
        icm20948init();
    }
    else
    {
        *penMotionSensorType = IMU_EN_SENSOR_TYPE_NULL;
    }

    bRet = bmp280Check();
    if (true == bRet)
    {
        *penPressureType = IMU_EN_SENSOR_TYPE_BMP280;
        bmp280Init();
    }
    else
    {
        *penPressureType = IMU_EN_SENSOR_TYPE_NULL;
    }

    q0 = 1.0f;
    q1 = 0.0f;
    q2 = 0.0f;
    q3 = 0.0f;

    return;
}

void ICM20948::imuDataGet(IMU_ST_ANGLES_DATA *pstAngles,
                          IMU_ST_SENSOR_DATA *pstGyroRawData,
                          IMU_ST_SENSOR_DATA *pstAcceRawData,
                          IMU_ST_SENSOR_DATA *pstMagnRawData)
{
    int16_t s16Gyro[3], s16Accel[3], s16Magn[3];

    icm20948AccelRead(&s16Accel[0], &s16Accel[1], &s16Accel[2]);
    icm20948GyroRead(&s16Gyro[0], &s16Gyro[1], &s16Gyro[2]);
    icm20948MagRead(&s16Magn[0], &s16Magn[1], &s16Magn[2]);

    // adapt imu axis with board
    {
        pstGyroRawData->s16Y = s16Gyro[0];
        pstGyroRawData->s16X = -s16Gyro[1];
        pstGyroRawData->s16Z = s16Gyro[2];

        pstAcceRawData->s16Y = s16Accel[0];
        pstAcceRawData->s16X = -s16Accel[1];
        pstAcceRawData->s16Z = s16Accel[2];

        pstMagnRawData->s16Y = s16Magn[0];
        pstMagnRawData->s16X = -s16Magn[1];
        pstMagnRawData->s16Z = s16Magn[2];
    }

    if (_attInitialized == 0)
    {
        // TODO: initialise quaternion
        _attInitialized = 1;
    }

    if (_attInitialized == 1)
    {
        // s16Gyro / x -> (dps)     250dps: x=131   500dps: x=65.5  1000dps: x=32.8     2000dps: x=16.4
        // s16Accel / x -> (g)      2g: x=16384     4g: x=8192      8g: x=4096          16g: x=2048
        // s16Magn * 0.15 -> (uT)
        imuAHRSupdate(pstGyroRawData->s16X * deg2rad / 32.8, pstGyroRawData->s16Y * deg2rad / 32.8, pstGyroRawData->s16Z * deg2rad / 32.8,
                      pstAcceRawData->s16X / 16384.0, pstAcceRawData->s16Y / 16384.0, pstAcceRawData->s16Z / 16384.0,
                      pstMagnRawData->s16X * 0.15, pstMagnRawData->s16Y * 0.15, pstMagnRawData->s16Z * 0.15);

        pstAngles->fPitch = asin(-2 * q1 * q3 + 2 * q0 * q2) * 57.3;                                // pitch
        pstAngles->fRoll = atan2(2 * q2 * q3 + 2 * q0 * q1, -2 * q1 * q1 - 2 * q2 * q2 + 1) * 57.3; // roll
        pstAngles->fYaw = atan2(-2 * q1 * q2 - 2 * q0 * q3, 2 * q2 * q2 + 2 * q3 * q3 - 1) * 57.3;  // yaw
    }

    return;
}

void ICM20948::pressSensorDataGet(int32_t *ps32Temperature, int32_t *ps32Pressure, int32_t *ps32Altitude)
{
    float CurPressure, CurTemperature;
    int32_t CurAltitude;
    static BMP280_AvgTypeDef BMP280_Filter[3];

    bmp280TandPGet(&CurTemperature, &CurPressure);
    bmp280CalAvgValue(&BMP280_Filter[0].Index, BMP280_Filter[0].AvgBuffer, (int32_t)(CurPressure), ps32Pressure);
    bmp280CalculateAbsoluteAltitude(&CurAltitude, (*ps32Pressure));
    bmp280CalAvgValue(&BMP280_Filter[1].Index, BMP280_Filter[1].AvgBuffer, CurAltitude, ps32Altitude);
    bmp280CalAvgValue(&BMP280_Filter[2].Index, BMP280_Filter[2].AvgBuffer, (int32_t)CurTemperature, ps32Temperature);

    return;
}

// private
uint8_t ICM20948::I2C_ReadOneByte(uint8_t DevAddr, uint8_t RegAddr)
{
    uint8_t u8Ret[1] = {0};
    HAL_I2C_Mem_Read(&hi2c3, DevAddr, RegAddr, I2C_MEMADD_SIZE_8BIT, u8Ret, 1, 1000);

    return u8Ret[0];
}

void ICM20948::I2C_WriteOneByte(uint8_t DevAddr, uint8_t RegAddr, uint8_t value)
{
    uint8_t buf[2] = {0};
    buf[0] = RegAddr;
    buf[1] = value;
    HAL_I2C_Master_Transmit(&hi2c3, DevAddr, buf, 2, 100);
}

void ICM20948::icm20948init(void)
{
    /* user bank 0 register */
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_PWR_MIGMT_1, REG_VAL_ALL_RGE_RESET);
    HAL_Delay(10);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_PWR_MIGMT_1, REG_VAL_RUN_MODE);

    /* user bank 2 register */
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_2);
    // 1.1 kHz/(1+GYRO_SMPLRT_DIV[7:0]) = 220Hz
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_SMPLRT_DIV, 0x04);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_CONFIG_1,
                     REG_VAL_BIT_GYRO_DLPCFG_6 | REG_VAL_BIT_GYRO_FS_1000DPS | REG_VAL_BIT_GYRO_DLPF);
    // 1.125 kHz/(1+ACCEL_SMPLRT_DIV[11:0]) = 225Hz
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_SMPLRT_DIV_2, 0x04);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_CONFIG,
                     REG_VAL_BIT_ACCEL_DLPCFG_6 | REG_VAL_BIT_ACCEL_FS_2g | REG_VAL_BIT_ACCEL_DLPF);

    /* user bank 0 register */
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0);
    HAL_Delay(100);

    /* offset */
    icm20948GyroOffset();
    icm20948MagCheck();
    icm20948WriteSecondary(I2C_ADD_ICM20948_AK09916 | I2C_ADD_ICM20948_AK09916_WRITE,
                           REG_ADD_MAG_CNTL2, REG_VAL_MAG_MODE_20HZ);

    return;
}

bool ICM20948::icm20948Check(void)
{
    bool bRet = false;
    if (REG_VAL_WIA == I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_WIA))
    {
        bRet = true;
    }

    return bRet;
}

bool ICM20948::icm20948MagCheck(void)
{
    bool bRet = false;
    uint8_t u8Ret[2];

    icm20948ReadSecondary(I2C_ADD_ICM20948_AK09916 | I2C_ADD_ICM20948_AK09916_READ,
                          REG_ADD_MAG_WIA1, 2, u8Ret);
    if ((u8Ret[0] == REG_VAL_MAG_WIA1) && (u8Ret[1] == REG_VAL_MAG_WIA2))
    {
        bRet = true;
    }

    return bRet;
}

void ICM20948::icm20948GyroRead(int16_t *ps16X, int16_t *ps16Y, int16_t *ps16Z)
{
    uint8_t u8Buf[6];
    int16_t s16Buf[3] = {0};
    uint8_t i;
    int32_t s32OutBuf[3] = {0};
    static ICM20948_ST_AVG_DATA sstAvgBuf[3];
    static int16_t ss16c = 0;
    ss16c++;

    u8Buf[0] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_XOUT_L);
    u8Buf[1] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_XOUT_H);
    s16Buf[0] = (u8Buf[1] << 8) | u8Buf[0];

    u8Buf[0] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_YOUT_L);
    u8Buf[1] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_YOUT_H);
    s16Buf[1] = (u8Buf[1] << 8) | u8Buf[0];

    u8Buf[0] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_ZOUT_L);
    u8Buf[1] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_GYRO_ZOUT_H);
    s16Buf[2] = (u8Buf[1] << 8) | u8Buf[0];

    for (i = 0; i < 3; i++)
    {
        icm20948CalAvgValue(&sstAvgBuf[i].u8Index, sstAvgBuf[i].s16AvgBuffer, s16Buf[i], s32OutBuf + i);
    }
    *ps16X = s32OutBuf[0] - gstGyroOffset.s16X;
    *ps16Y = s32OutBuf[1] - gstGyroOffset.s16Y;
    *ps16Z = s32OutBuf[2] - gstGyroOffset.s16Z;

    return;
}

void ICM20948::icm20948AccelRead(int16_t *ps16X, int16_t *ps16Y, int16_t *ps16Z)
{
    uint8_t u8Buf[2];
    int16_t s16Buf[3] = {0};
    uint8_t i;
    int32_t s32OutBuf[3] = {0};
    static ICM20948_ST_AVG_DATA sstAvgBuf[3];

    u8Buf[0] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_XOUT_L);
    u8Buf[1] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_XOUT_H);
    s16Buf[0] = (u8Buf[1] << 8) | u8Buf[0];

    u8Buf[0] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_YOUT_L);
    u8Buf[1] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_YOUT_H);
    s16Buf[1] = (u8Buf[1] << 8) | u8Buf[0];

    u8Buf[0] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_ZOUT_L);
    u8Buf[1] = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_ACCEL_ZOUT_H);
    s16Buf[2] = (u8Buf[1] << 8) | u8Buf[0];

    for (i = 0; i < 3; i++)
    {
        icm20948CalAvgValue(&sstAvgBuf[i].u8Index, sstAvgBuf[i].s16AvgBuffer, s16Buf[i], s32OutBuf + i);
    }
    *ps16X = s32OutBuf[0];
    *ps16Y = s32OutBuf[1];
    *ps16Z = s32OutBuf[2];

    return;
}

void ICM20948::icm20948MagRead(int16_t *ps16X, int16_t *ps16Y, int16_t *ps16Z)
{
    uint8_t counter = 20;
    uint8_t u8Data[MAG_DATA_LEN];
    int16_t s16Buf[3] = {0};
    uint8_t i;
    int32_t s32OutBuf[3] = {0};
    static ICM20948_ST_AVG_DATA sstAvgBuf[3];
    while (counter > 0)
    {
        HAL_Delay(10);
        icm20948ReadSecondary(I2C_ADD_ICM20948_AK09916 | I2C_ADD_ICM20948_AK09916_READ,
                              REG_ADD_MAG_ST2, 1, u8Data);

        if ((u8Data[0] & 0x01) != 0)
            break;

        counter--;
    }

    if (counter != 0)
    {
        icm20948ReadSecondary(I2C_ADD_ICM20948_AK09916 | I2C_ADD_ICM20948_AK09916_READ,
                              REG_ADD_MAG_DATA,
                              MAG_DATA_LEN,
                              u8Data);
        s16Buf[0] = ((int16_t)u8Data[1] << 8) | u8Data[0];
        s16Buf[1] = ((int16_t)u8Data[3] << 8) | u8Data[2];
        s16Buf[2] = ((int16_t)u8Data[5] << 8) | u8Data[4];
    }

    for (i = 0; i < 3; i++)
    {
        icm20948CalAvgValue(&sstAvgBuf[i].u8Index, sstAvgBuf[i].s16AvgBuffer, s16Buf[i], s32OutBuf + i);
    }

    *ps16X = s32OutBuf[0];
    *ps16Y = -s32OutBuf[1];
    *ps16Z = -s32OutBuf[2];

    return;
}

void ICM20948::icm20948ReadSecondary(uint8_t u8I2CAddr, uint8_t u8RegAddr, uint8_t u8Len, uint8_t *pu8data)
{
    uint8_t i;
    uint8_t u8Temp;

    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3); // swtich bank3
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_ADDR, u8I2CAddr);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_REG, u8RegAddr);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL, REG_VAL_BIT_SLV0_EN | u8Len);

    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0); // swtich bank0

    u8Temp = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL);
    u8Temp |= REG_VAL_BIT_I2C_MST_EN;
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL, u8Temp);
    HAL_Delay(5);
    u8Temp &= ~REG_VAL_BIT_I2C_MST_EN;
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL, u8Temp);

    for (i = 0; i < u8Len; i++)
    {
        *(pu8data + i) = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_EXT_SENS_DATA_00 + i);
    }
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3); // swtich bank3

    u8Temp = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL);
    u8Temp &= ~((REG_VAL_BIT_I2C_MST_EN) & (REG_VAL_BIT_MASK_LEN));
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL, u8Temp);

    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0); // swtich bank0
}

void ICM20948::icm20948WriteSecondary(uint8_t u8I2CAddr, uint8_t u8RegAddr, uint8_t u8data)
{
    uint8_t u8Temp;
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3); // swtich bank3
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV1_ADDR, u8I2CAddr);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV1_REG, u8RegAddr);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV1_DO, u8data);
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV1_CTRL, REG_VAL_BIT_SLV0_EN | 1);

    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0); // swtich bank0

    u8Temp = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL);
    u8Temp |= REG_VAL_BIT_I2C_MST_EN;
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL, u8Temp);
    HAL_Delay(5);
    u8Temp &= ~REG_VAL_BIT_I2C_MST_EN;
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_USER_CTRL, u8Temp);

    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_3); // swtich bank3

    u8Temp = I2C_ReadOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL);
    u8Temp &= ~((REG_VAL_BIT_I2C_MST_EN) & (REG_VAL_BIT_MASK_LEN));
    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_I2C_SLV0_CTRL, u8Temp);

    I2C_WriteOneByte(I2C_ADD_ICM20948, REG_ADD_REG_BANK_SEL, REG_VAL_REG_BANK_0); // swtich bank0

    return;
}

void ICM20948::icm20948GyroOffset(void)
{
    uint8_t i;
    int16_t s16Gx = 0, s16Gy = 0, s16Gz = 0;
    int32_t s32TempGx = 0, s32TempGy = 0, s32TempGz = 0;
    for (i = 0; i < 32; i++)
    {
        icm20948GyroRead(&s16Gx, &s16Gy, &s16Gz);
        s32TempGx += s16Gx;
        s32TempGy += s16Gy;
        s32TempGz += s16Gz;
        HAL_Delay(30);
    }
    gstGyroOffset.s16X = s32TempGx >> 5;
    gstGyroOffset.s16Y = s32TempGy >> 5;
    gstGyroOffset.s16Z = s32TempGz >> 5;

    return;
}

void ICM20948::icm20948CalAvgValue(uint8_t *pIndex, int16_t *pAvgBuffer, int16_t InVal, int32_t *pOutVal)
{
    uint8_t i;

    *(pAvgBuffer + ((*pIndex)++)) = InVal;
    *pIndex &= 0x07;

    *pOutVal = 0;
    for (i = 0; i < 8; i++)
    {
        *pOutVal += *(pAvgBuffer + i);
    }
    *pOutVal >>= 3;
}

void ICM20948::imuAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz)
{
    float norm;
    float hx, hy, hz, bx, bz;
    float vx, vy, vz, wx, wy, wz;
    float exInt = 0.0, eyInt = 0.0, ezInt = 0.0;
    float ex, ey, ez, halfT = 0.024f;

    float q0q0 = q0 * q0;
    float q0q1 = q0 * q1;
    float q0q2 = q0 * q2;
    float q0q3 = q0 * q3;
    float q1q1 = q1 * q1;
    float q1q2 = q1 * q2;
    float q1q3 = q1 * q3;
    float q2q2 = q2 * q2;
    float q2q3 = q2 * q3;
    float q3q3 = q3 * q3;

    norm = invSqrt(ax * ax + ay * ay + az * az);
    ax = ax * norm;
    ay = ay * norm;
    az = az * norm;

    norm = invSqrt(mx * mx + my * my + mz * mz);
    mx = mx * norm;
    my = my * norm;
    mz = mz * norm;

    // compute reference direction of flux
    hx = 2 * mx * (0.5f - q2q2 - q3q3) + 2 * my * (q1q2 - q0q3) + 2 * mz * (q1q3 + q0q2);
    hy = 2 * mx * (q1q2 + q0q3) + 2 * my * (0.5f - q1q1 - q3q3) + 2 * mz * (q2q3 - q0q1);
    hz = 2 * mx * (q1q3 - q0q2) + 2 * my * (q2q3 + q0q1) + 2 * mz * (0.5f - q1q1 - q2q2);
    bx = sqrt((hx * hx) + (hy * hy));
    bz = hz;

    // estimated direction of gravity and flux (v and w)
    vx = 2 * (q1q3 - q0q2);
    vy = 2 * (q0q1 + q2q3);
    vz = q0q0 - q1q1 - q2q2 + q3q3;
    wx = 2 * bx * (0.5 - q2q2 - q3q3) + 2 * bz * (q1q3 - q0q2);
    wy = 2 * bx * (q1q2 - q0q3) + 2 * bz * (q0q1 + q2q3);
    wz = 2 * bx * (q0q2 + q1q3) + 2 * bz * (0.5 - q1q1 - q2q2);

    // error is sum of cross product between reference direction of fields and direction measured by sensors
    ex = (ay * vz - az * vy) + (my * wz - mz * wy);
    ey = (az * vx - ax * vz) + (mz * wx - mx * wz);
    ez = (ax * vy - ay * vx) + (mx * wy - my * wx);

    if (ex != 0.0f && ey != 0.0f && ez != 0.0f)
    {
        exInt = exInt + ex * Ki * halfT;
        eyInt = eyInt + ey * Ki * halfT;
        ezInt = ezInt + ez * Ki * halfT;

        gx = gx + Kp * ex + exInt;
        gy = gy + Kp * ey + eyInt;
        gz = gz + Kp * ez + ezInt;
    }

    q0 = q0 + (-q1 * gx - q2 * gy - q3 * gz) * halfT;
    q1 = q1 + (q0 * gx + q2 * gz - q3 * gy) * halfT;
    q2 = q2 + (q0 * gy - q1 * gz + q3 * gx) * halfT;
    q3 = q3 + (q0 * gz + q1 * gy - q2 * gx) * halfT;

    norm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 = q0 * norm;
    q1 = q1 * norm;
    q2 = q2 * norm;
    q3 = q3 * norm;
}

float ICM20948::invSqrt(float x)
{
    float halfx = 0.5f * x;
    float y = x;

    long i = *(long *)&y;             // get bits for floating value
    i = 0x5f3759df - (i >> 1);        // gives initial guss you
    y = *(float *)&i;                 // convert bits back to float
    y = y * (1.5f - (halfx * y * y)); // newtop step, repeating increases accuracy

    return y;
}

void ICM20948::bmp280Init(void)
{
    I2C_WriteOneByte(BMP280_ADDR, BMP280_REGISTER_CONTROL, 0xFF);
    I2C_WriteOneByte(BMP280_ADDR, BMP280_REGISTER_CONFIG, 0x14);
    bmp280ReadCalibration();
}

bool ICM20948::bmp280Check(void)
{
    bool bRet = false;
    if (0x58 == I2C_ReadOneByte(BMP280_ADDR, BMP280_REGISTER_CHIPID))
    {
        bRet = true;
    }

    return bRet;
}

void ICM20948::bmp280ReadCalibration(void)
{
    uint8_t lsb, msb;

    /* read the temperature calibration parameters */
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_T1_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_T1_MSB_REG);
    dig_T1 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_T2_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_T2_MSB_REG);
    dig_T2 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_T3_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_T3_MSB_REG);
    dig_T3 = msb << 8 | lsb;

    /* read the pressure calibration parameters */
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P1_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P1_MSB_REG);
    dig_P1 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P2_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P2_MSB_REG);
    dig_P2 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P3_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P3_MSB_REG);
    dig_P3 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P4_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P4_MSB_REG);
    dig_P4 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P5_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P5_MSB_REG);
    dig_P5 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P6_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P6_MSB_REG);
    dig_P6 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P7_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P7_MSB_REG);
    dig_P7 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P8_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P8_MSB_REG);
    dig_P8 = msb << 8 | lsb;
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P9_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_DIG_P9_MSB_REG);
    dig_P9 = msb << 8 | lsb;
}

void ICM20948::bmp280TandPGet(float *temperature, float *pressure)
{
    uint8_t lsb, msb, xlsb;
    int32_t adc_P, adc_T;

    xlsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_TEMP_XLSB_REG);
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_TEMP_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_TEMP_MSB_REG);

    // adc_T = (msb << 12) | (lsb << 4) | (xlsb >> 4);
    adc_T = msb;
    adc_T <<= 8;
    adc_T |= lsb;
    adc_T <<= 8;
    adc_T |= xlsb;
    adc_T >>= 4;

    // adc_T = 415148;
    *temperature = bmp280CompensateTemperature(adc_T);

    xlsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_PRESS_XLSB_REG);
    lsb = I2C_ReadOneByte(BMP280_ADDR, BMP280_PRESS_LSB_REG);
    msb = I2C_ReadOneByte(BMP280_ADDR, BMP280_PRESS_MSB_REG);
    // adc_P = (msb << 12) | (lsb << 4) | (xlsb >> 4);
    adc_P = msb;
    adc_P <<= 8;
    adc_P |= lsb;
    adc_P <<= 8;
    adc_P |= xlsb;
    adc_P >>= 4;

    // adc_P = 51988;
    *pressure = bmp280CompensatePressure(adc_P);
}

void ICM20948::bmp280CalAvgValue(uint8_t *pIndex, int32_t *pAvgBuffer, int32_t InVal, int32_t *pOutVal)
{
    uint8_t i;

    *(pAvgBuffer + ((*pIndex)++)) = InVal;
    *pIndex &= 0x07;

    *pOutVal = 0;
    for (i = 0; i < 8; i++)
    {
        *pOutVal += *(pAvgBuffer + i);
    }
    *pOutVal >>= 3;
}

void ICM20948::bmp280CalculateAbsoluteAltitude(int32_t *pAltitude, int32_t PressureVal)
{
    *pAltitude = 4433000 * (1 - pow((PressureVal / (float)gs32Pressure0), 0.1903));
}

float ICM20948::bmp280CompensateTemperature(int32_t adc_T)
{
    int64_t var1, var2, temperature;

    var1 = ((((adc_T >> 3) - ((int64_t)dig_T1 << 1))) * ((int64_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int64_t)dig_T1)) * ((adc_T >> 4) - ((int64_t)dig_T1))) >> 12) *
            ((int64_t)dig_T3)) >>
           14;
    t_fine = var1 + var2;

    temperature = (t_fine * 5 + 128) >> 8;

    return (float)temperature;
}

float ICM20948::bmp280CompensatePressure(int32_t adc_P)
{
    int64_t var1, var2;
    uint64_t pressure;
#if 1
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

    if (var1 == 0)
    {
        return 0; // avoid exception caused by division by zero
    }

    pressure = 1048576.0 - adc_P;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    return (float)pressure / 256;
#else
    var1 = (((int64_t)t_fine) >> 1) - (int64_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int64_t)dig_P6);
    var2 = var2 + ((var1 * ((int64_t)dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int64_t)dig_P4) << 16);
    var1 = (((dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int64_t)dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int64_t)dig_P1)) >> 15);
    if (var1 == 0)
    {
        return 0;
    }
    pressure = (1048576.0 - adc_P) - (var2 >> 12) * 3125;
    if (pressure < 0x80000000)
    {
        pressure = (pressure << 1) / ((uint64_t)var1);
    }
    else
    {
        pressure = (pressure / (uint64_t)var1) * 2;
    }
    var1 = (((int64_t)dig_P9) * ((int64_t)(((pressure >> 3) * (pressure >> 3)) >> 13))) >> 12;
    var2 = (((int64_t)(pressure >> 2)) * ((int64_t)dig_P8)) >> 13;
    pressure = (uint64_t)((int64_t)pressure) + ((var1 + var2 + dig_P7) >> 4);
    return (float)pressure;
#endif
}
