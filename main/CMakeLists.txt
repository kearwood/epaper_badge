set(COMPONENT_SRCS "main.cpp, EPD_2in9b.c, DEV_Config.c, EPD_2in9b.c, font12.c, font20.c, font24.c, font8.c, GUI_BMPfile.c, GUI_Paint.c, ImageData.c")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()

# Create a SPIFFS image from the contents of the 'spiffs_image' directory
# that fits the partition named 'storage'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py flash'. 
spiffs_create_partition_image(storage ../spiffs_image FLASH_IN_PROJECT)