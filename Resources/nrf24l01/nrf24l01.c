#include "nrf24l01.h"
#include "nrf_spi.h"
#include "string.h"
#include "delay.h"

/**
 * Gửi một lệnh (cmd) kèm dữ liệu tới NRF qua SPI.
 * - Trình tự: CSN=0 -> gửi cmd -> gửi len byte dữ liệu (nếu có) -> CSN=1
 *
 * Tham số:
 * - cmd  : mã lệnh NRF (ví dụ NRF_CMD_W_REGISTER | reg)
 * - value: con trỏ dữ liệu kèm theo
 * - len  : số byte dữ liệu kèm theo
 *
 * Lưu ý: Nhánh len==1 sẽ thực hiện lấy giá trị của value 
 * chứ không phải lấy giá trị tại địa chỉ chứa trong value.
 */
void NRF_WriteCmd(uint8_t cmd, uint8_t *value, uint8_t len)
{
	// Kéo CSN xuống thấp để bắt đầu phiên SPI với NRF24L01
	NRF_CSN_LOW();
	// Gửi byte lệnh đầu tiên
	SPI_Transfer(cmd);
	if(len == 1)
	{
		// Trường hợp chỉ có 1 byte dữ liệu đính kèm lệnh
		// Lưu ý: gọi SPI_Transfer(value) sẽ truyền giá trị con trỏ, không phải byte dữ liệu
		SPI_Transfer(value); 
	}
	else 
	{
		// Gửi lần lượt từng byte, sau mỗi lần gửi con trỏ tăng 1
		while(len--) SPI_Transfer(*value++); 
	}
	// Kéo CSN lên cao để kết thúc phiên SPI
	NRF_CSN_HIGH();
}

/**
 * Đọc dữ liệu từ NRF bằng cách gửi cmd đọc rồi clock ra các byte bằng NOP.
 *
 * Tham số:
 * - cmd  : lệnh đọc (ví dụ NRF_CMD_R_REGISTER | reg)
 * - value: buffer đích để lưu dữ liệu đọc về
 * - len  : số byte cần đọc
 */
void NRF_ReadCmd(uint8_t cmd, uint8_t *value, uint8_t len)
{
	// Bắt đầu giao tiếp SPI
	NRF_CSN_LOW();
	// Gửi lệnh đọc trước
	SPI_Transfer(cmd);
	// Mỗi byte nhận về tương ứng 1 lần clock ra một byte NOP
	while(len--) *value++ = SPI_Transfer(NRF_CMD_NOP); 
	// Kết thúc giao tiếp SPI
	NRF_CSN_HIGH();
}

/** Ghi một thanh ghi 8-bit. */
void NRF_WriteReg_WithOneByte(uint8_t reg, uint8_t value) 
{
	// Ghi 1 byte vào thanh ghi: cmd = W_REGISTER | reg, dữ liệu là 1 byte
	NRF_WriteCmd(NRF_CMD_W_REGISTER | reg, value, 1);
}

/** Đọc một thanh ghi 8-bit và trả về giá trị. */
uint8_t NRF_ReadReg_WithOneByte(uint8_t reg) 
{
	uint8_t data;
	// Đọc 1 byte từ thanh ghi: cmd = R_REGISTER | reg
	NRF_ReadCmd(NRF_CMD_R_REGISTER | reg, &data, 1);
	// Trả về giá trị đọc được
	return data;
}

/**
 * Set/Clear một bit trong thanh ghi 8-bit.
 * - value=1 -> set bit; value=0 -> clear bit.
 */
void NRF_WriteReg_WithOneBit(uint8_t reg, uint8_t bit, uint8_t value)
{
	// 1) Đọc giá trị hiện tại của thanh ghi
	uint8_t reg_value = NRF_ReadReg_WithOneByte(reg);

	// 2) Set/Clear bit theo tham số value
	reg_value = value ? (reg_value | bit) : (reg_value & ~bit);

	// 3) Ghi trả lại thanh ghi với giá trị mới
	NRF_WriteReg_WithOneByte(reg, reg_value);
}

/** Kiểm tra một bit trong thanh ghi 8-bit, trả về 1 nếu đang set, ngược lại 0. */
uint8_t NRF_ReadReg_WithOneBit(uint8_t reg, uint8_t bit)
{
	uint8_t reg_value = NRF_ReadReg_WithOneByte(reg);
	// Trả về 1 nếu bit được set, ngược lại 0
	return (reg_value & bit) && 1;
}

/** Ghi nhiều byte vào một thanh ghi (ví dụ địa chỉ TX/RX). */
void NRF_WriteReg_WithMultiBytes(uint8_t reg, uint8_t *data, uint8_t len) 
{
	// Ghi nhiều byte liền kề (ví dụ địa chỉ 3-5 byte)
	NRF_WriteCmd(NRF_CMD_W_REGISTER | reg, data, len);
}

/** Đọc nhiều byte từ một thanh ghi (ví dụ địa chỉ TX/RX). */
void NRF_ReadReg_WithMultiBytes(uint8_t reg, uint8_t *data, uint8_t len) 
{
	// Đọc nhiều byte liên tiếp từ một thanh ghi bắt đầu tại reg
	NRF_ReadCmd(NRF_CMD_R_REGISTER | reg, data, len);
}


/** Flush FIFO RX (xóa dữ liệu hàng đợi nhận). */
void NRF_Flush_RX(void)
{
	// Xóa toàn bộ FIFO nhận để tránh đọc dữ liệu cũ/lỗi
	NRF_CSN_LOW();
	SPI_Transfer(NRF_CMD_FLUSH_RX);
	NRF_CSN_HIGH();
}

/** Flush FIFO TX (xóa dữ liệu hàng đợi phát). */
void NRF_Flush_TX(void)
{
	// Xóa toàn bộ FIFO phát để chuẩn bị nạp payload mới
	NRF_CSN_LOW();
	SPI_Transfer(NRF_CMD_FLUSH_TX);
	NRF_CSN_HIGH();
}

/** Đọc thanh ghi STATUS bằng cách clock lệnh NOP; trả về byte STATUS. */
uint8_t NRF_ReadStatus(void)
{
	uint8_t status;
	// Đọc STATUS bằng cách clock 1 byte NOP khi CSN thấp
	NRF_CSN_LOW();
	status = SPI_Transfer(NRF_CMD_NOP); // đọc STATUS
	NRF_CSN_HIGH();
	return status;
}

/*------------------------------------------ TX Mode ------------------------------------------*/
#ifdef TX_MODE

/**
 * Khởi tạo NRF ở chế độ TX (phát): bật CRC, Auto-ACK, cấu hình địa chỉ/kênh,
 * set payload width, clear cờ STATUS, flush TX, về PWR_UP và CE thấp.
 *
 * Tham số:
 * - addr   : địa chỉ 5 byte cho TX_ADDR và RX_ADDR_P0 (dùng Auto-ACK)
 * - channel: kênh RF (0..127)
 */
void NRF_TX_Mode_Init(const uint8_t *addr, const uint8_t channel)
{
	SPI_Open(); // Mở SPI trước khi cấu hình NRF
	delay_ms(20);
	
	// Bật CRC (1 hoặc 2 byte tùy CONFIG), PRIM_RX=0 (TX mode)
	NRF_WriteReg_WithOneByte(NRF_REG_CONFIG, 				CONFIG_EN_CRC); // CONFIG: EN_CRC = 1
	// Bật Auto-ACK cho pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_AA, 				ENAA_P0); // EN_AA: auto-ack
	// Bật địa chỉ pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_RXADDR, 		ERX_P0); // EN_RXADDR: enable pipe0
	// Thiết lập độ dài địa chỉ (5 byte): theo datasheet lưu dưới dạng (width-2)
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_AW, 			(ADDRESS_LENGTH - 0x02)); // SETUP_AW: 5 bytes addr
	// Thiết lập tự động retry: delay ~1000us, tối đa 15 lần
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_RETR, 		0x3f); // SETUP_RETR: 1000us, 15 retries
	// Chọn kênh RF (0..127)
	NRF_WriteReg_WithOneByte(NRF_REG_RF_CH, 				channel & 0x7F);   // RF_CH: channel
	
	// Ghi địa chỉ phát (TX_ADDR)
	NRF_WriteReg_WithMultiBytes(NRF_REG_TX_ADDR, addr, ADDRESS_LENGTH); 
	
	// RX_ADDR_P0 phải trùng với địa chỉ TX để Auto-ACK hoạt động
	NRF_WriteReg_WithMultiBytes(NRF_REG_RX_ADDR_P0, addr, ADDRESS_LENGTH); 
	
	// Cấu hình kích thước payload cố định cho pipe0
	NRF_WriteReg_WithOneByte(NRF_REG_RX_PW_P0, PACKET_SIZE);  
	
	// Clear cờ trong STATUS
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS, 		STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);
	
	NRF_Flush_TX();
	
	// TX mode: CE = 0, chỉ pulse khi gửi
	NRF_CE_LOW();

	// Bật nguồn RF (PWR_UP) và đợi mạch ổn định
	NRF_WriteReg_WithOneBit(NRF_REG_CONFIG, CONFIG_PWR_UP, 1); // CONFIG: PWR_UP=1
	delay_ms(10);
}

/**
 * Gửi một gói dữ liệu ở TX mode.
 * - Clamp độ dài về PACKET_SIZE, ghi payload vào FIFO TX (W_TX_PAYLOAD),
 *   pulse CE >= ~10us để phát, chờ TX_DS hoặc MAX_RT rồi xóa cờ STATUS.
 */
void NRF_SendData(uint8_t *data, uint8_t len) 
{
	uint8_t temp[PACKET_SIZE];
	// Đảm bảo CE thấp trước khi nạp payload TX
	NRF_CE_LOW();

	// Xóa FIFO TX để tránh chèn payload vào hàng đợi cũ
	NRF_Flush_TX();

	// Giới hạn độ dài về PACKET_SIZE
	len = len > PACKET_SIZE ? PACKET_SIZE : len;
	// Điền 0 phần còn lại để đủ kích thước cố định
	memset(temp, 0, PACKET_SIZE);
	// Sao chép dữ liệu người dùng vào buffer tạm
	memmove(temp, data, len);
	// Nạp payload vào FIFO TX
	NRF_WriteCmd(NRF_CMD_W_TX_PAYLOAD, temp, PACKET_SIZE);

	// Pulse CE để phát
	NRF_CE_HIGH();
	// Giữ CE mức cao tối thiểu ~10us để bắt đầu truyền
	delay_us(50); // delay ngắn ~10us
	NRF_CE_LOW();

	// Đợi TX_DS hoặc MAX_RT
	while (!(NRF_ReadStatus() & (STATUS_TX_DS | STATUS_MAX_RT)));

	// Xóa cờ
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS, 0x70);
}

/*------------------------------------------ RX Mode ------------------------------------------*/
#else

/**
 * Khởi tạo NRF ở chế độ RX (nghe/nhận): bật CRC, PRIM_RX, Auto-ACK, địa chỉ,
 * kênh, RF_SETUP, payload width; clear cờ STATUS, bật PWR_UP, flush RX, CE=1.
 *
 * Tham số:
 * - addr   : địa chỉ 5 byte cho RX_ADDR_P0 (và TX_ADDR để ACK trả về)
 * - channel: kênh RF (0..127)
 */
void NRF_RX_Mode_Init(const uint8_t *addr, const uint8_t channel)
{
	SPI_Open(); // Mở SPI trước khi cấu hình NRF
	delay_ms(20);
	
	// Bật CRC, chuyển về chế độ nhận (PRIM_RX)
	NRF_WriteReg_WithOneByte(NRF_REG_CONFIG,				CONFIG_EN_CRC | CONFIG_PRIM_RX); // CONFIG: EN_CRC = 1, PWR_UP=1, PRIM_RX=1
	// Bật Auto-ACK cho pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_AA,					ENAA_P0); // EN_AA: auto-ack
	// Bật địa chỉ pipe 0
	NRF_WriteReg_WithOneByte(NRF_REG_EN_RXADDR,			ERX_P0); // EN_RXADDR: enable pipe0
	// Độ dài địa chỉ = 5 byte
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_AW,			(ADDRESS_LENGTH - 0x02)); // SETUP_AW: 5 bytes addr
	// Retry dùng cho chế độ TX (cũng thiết lập trước)
	NRF_WriteReg_WithOneByte(NRF_REG_SETUP_RETR,		0x3f); // SETUP_RETR: 1000us, 15 retries
	// Kênh RF
	NRF_WriteReg_WithOneByte(NRF_REG_RF_CH,					channel & 0x7F);   // RF_CH: channel
	// Tốc độ 1Mbps, công suất 0dBm (0x0f phụ thuộc cấu hình DATASHEET)
	NRF_WriteReg_WithOneByte(NRF_REG_RF_SETUP,			0x0f); // RF_SETUP: 1Mbps, 0dBm
	
	// Thiết lập TX_ADDR (dùng cho trả ACK) và RX_ADDR_P0 trùng nhau
	NRF_WriteReg_WithMultiBytes(NRF_REG_TX_ADDR, addr, ADDRESS_LENGTH); 
	
	NRF_WriteReg_WithMultiBytes(NRF_REG_RX_ADDR_P0, addr, ADDRESS_LENGTH); 
	
	// Set payload width cho pipe0 (32 byte)
	NRF_WriteReg_WithOneByte(NRF_REG_RX_PW_P0, PACKET_SIZE); 
	
	// Clear interrupt flags trong STATUS
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS,  	(STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT));
	
	// Bật nguồn RF
	NRF_WriteReg_WithOneBit(NRF_REG_CONFIG,	CONFIG_PWR_UP, 1);
	delay_ms(10);
	
	// Đảm bảo FIFO RX sạch trước khi bắt đầu lắng nghe
	NRF_Flush_RX();

	NRF_StopListening();
}

/** Bắt đầu lắng nghe (CE = 1 ở RX mode). */
void NRF_StartListening(void)
{
	// Kéo CE lên cao để bắt đầu lắng nghe (PRX)
	NRF_CE_HIGH();
}

/** Dừng lắng nghe (CE = 0). */
void NRF_StopListening(void)
{
	// Kéo CE xuống thấp để dừng lắng nghe
	NRF_CE_LOW();
}


/** Trả về khác 0 nếu có dữ liệu sẵn (cờ RX_DR set). */
uint8_t NRF_DataReady(void) 
{
	uint8_t status = NRF_ReadStatus();
	// Kiểm tra cờ RX_DR; lưu ý không cho biết pipe nào, muốn biết pipe cần đọc RX_P_NO
	return status & STATUS_RX_DR;
}

/** Đọc payload từ FIFO RX (R_RX_PAYLOAD). */
void NRF_Read_RX_Payload(uint8_t *data, uint8_t len)
{
	// Đọc ra len byte từ đầu FIFO RX; nếu len < kích thước payload, phần còn lại vẫn nằm trong FIFO
	NRF_ReadCmd(NRF_CMD_R_RX_PAYLOAD, data, len);
}

/**
 * Đọc dữ liệu cấp cao:
 * - Luôn đọc PACKET_SIZE byte vào buffer tạm
 * - Sao chép tối đa len byte ra buffer người dùng
 * - Xóa cờ RX_DR sau khi đọc
 */
void NRF_ReadData(uint8_t *data, uint8_t len) 
{
	uint8_t temp[PACKET_SIZE];
	// Luôn đọc đủ PACKET_SIZE từ FIFO RX vào buffer tạm
	NRF_Read_RX_Payload(temp, PACKET_SIZE);

	// Chỉ copy tối đa len byte cho người gọi
	len = len > PACKET_SIZE ? PACKET_SIZE : len;
	memmove(data, temp, len);

	// Xóa cờ RX_DR
	NRF_WriteReg_WithOneByte(NRF_REG_STATUS, STATUS_RX_DR);
}

#endif
