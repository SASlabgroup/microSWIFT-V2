################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (10.3-2021.10)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/ThreadX/app_threadx.c 

S_UPPER_SRCS += \
../Core/Src/ThreadX/tx_initialize_low_level.S 

C_DEPS += \
./Core/Src/ThreadX/app_threadx.d 

OBJS += \
./Core/Src/ThreadX/app_threadx.o \
./Core/Src/ThreadX/tx_initialize_low_level.o 

S_UPPER_DEPS += \
./Core/Src/ThreadX/tx_initialize_low_level.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/ThreadX/app_threadx.o: ../Core/Src/ThreadX/app_threadx.c Core/Src/ThreadX/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32U575xx -DTX_INCLUDE_USER_DEFINE_FILE -DTX_SINGLE_MODE_NON_SECURE=1 -c -I../Core/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../AZURE_RTOS/App -I../Middlewares/ST/threadx/common/inc -I../Middlewares/ST/threadx/ports/cortex_m33/gnu/inc -I../Middlewares/ST/threadx/utility/low_power -I"C:/Users/veteran/STM32CubeIDE/microSWIFT_V2/microSWIFT_U5/Core/Inc/Waves" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/Src/ThreadX/%.o: ../Core/Src/ThreadX/%.S Core/Src/ThreadX/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m33 -g3 -DDEBUG -DTX_SINGLE_MODE_NON_SECURE=1 -c -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-Core-2f-Src-2f-ThreadX

clean-Core-2f-Src-2f-ThreadX:
	-$(RM) ./Core/Src/ThreadX/app_threadx.d ./Core/Src/ThreadX/app_threadx.o ./Core/Src/ThreadX/app_threadx.su ./Core/Src/ThreadX/tx_initialize_low_level.d ./Core/Src/ThreadX/tx_initialize_low_level.o

.PHONY: clean-Core-2f-Src-2f-ThreadX

