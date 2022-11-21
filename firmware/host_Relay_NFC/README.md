# Changes required Arduino Serial Command

For this example to work properly, you must make changes to the library [Arduino Serial Command](https://github.com/kroimon/Arduino-SerialCommand)

Change in SerialCommand.h
```
// Size of the input buffer in bytes (maximum length of one command plus arguments)
#define SERIALCOMMAND_BUFFER 32
```
to
```
// Size of the input buffer in bytes (maximum length of one command plus arguments)
#define SERIALCOMMAND_BUFFER 255
```
Change in SerialCommand.cpp
```
strcpy(delim, " "); // strtok_r needs a null-terminated string
```
to
```
strcpy(delim, "-"); // strtok_r needs a null-terminated string
```
