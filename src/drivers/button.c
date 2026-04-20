#include "button.h"

static int fd = -1;

inline uint8_t Button_Config(void)
{
  fd = open(DEVICE, O_RDONLY);
  if (fd < 0)
  {
      perror("Failed to open button device");
      return 0; // Failed to configure button
  }
  return 1; // Button configured successfully
}

inline uint8_t Button_Read(void)
{
  char buf[16];
  ssize_t bytes_read = read(fd, buf, sizeof(buf));
  if (bytes_read < 0)
  {
      perror("Failed to read button state");
      return 0; // Assume button is not pressed on error
  }
  if (bytes_read >= sizeof(buf))
      bytes_read = sizeof(buf) - 1;
  
  buf[bytes_read] = '\0';
  if(buf[0] == '1')
  {
    return 1; // Button is pressed
  }
  return 0; // Button is not pressed
}

inline void Button_Deinit(void)
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}