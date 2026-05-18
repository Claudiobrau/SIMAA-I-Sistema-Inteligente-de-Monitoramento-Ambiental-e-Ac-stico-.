# SIMAA-I-Sistema-Inteligente-de-Monitoramento-Ambiental-e-Ac-stico-.
Código-fonte do Projeto Integrador V - Monitoramento de Qualidade do Ar e Ruído.
**Projeto Integrador V – Univesp**  
*Grupo 10 – Engenharia da Computação*

---

## Descrição do Projeto

O **SIMAA-I** é um sistema de baixo custo para monitoramento contínuo da qualidade do ar e do nível de pressão sonora em ambientes internos. Utiliza um microcontrolador **NodeMCU ESP8266** e sensores de temperatura/umidade (**AHT21**), qualidade do ar (**ENS160**) e ruído (**MAX9814**). Os dados são enviados a cada 15 segundos para a plataforma **ThingsBoard** via MQTT, permitindo visualização remota e análise temporal.

O grande diferencial deste projeto é a **calibração empírica do microfone** com relação logarítmica, que permite converter o sinal de pico a pico (Vpp) em decibéis (dB SPL) de forma precisa, sem saturação.

---

## Sensores Utilizados

| Sensor         | Função                          | Faixa de medição      | Interface       |
|----------------|---------------------------------|-----------------------|-----------------|
| AHT21          | Temperatura e umidade relativa  | -40 a +85°C / 0–100%  | I2C             |
| ENS160         | eCO2 (ppm), TVOC (ppb), AQI     | 400–65000 ppm / 0–65000 ppb | I2C       |
| MAX9814        | Nível de pressão sonora (dB)    | ~30–110 dB (ajustável) | Analógico (A0) |

---

## Diagrama de Ligação (Hardware)

| Componente | Pino no NodeMCU    | Observação                                           |
|------------|--------------------|------------------------------------------------------|
| AHT21 SDA  | GPIO4 (D2)         | I2C – compartilhado com ENS160                       |
| AHT21 SCL  | GPIO5 (D1)         | I2C – compartilhado com ENS160                       |
| ENS160 SDA | GPIO4 (D2)         | I2C                                                  |
| ENS160 SCL | GPIO5 (D1)         | I2C                                                  |
| MAX9814 OUT| A0                 | Sinal analógico do microfone                         |
| MAX9814 GAIN| 3.3V (VDD)        | **Ganho fixo em 40dB** – essencial para a calibração |
| MAX9814 AR  | NC (desconectado) | Atack Ratio = padrão (1:4)                           |
**Atenção:** O pino GAIN do MAX9814 deve ser ligado ao **VDD (3.3V)**. Isso configura o ganho interno para **40 dB**, garantindo a faixa linear usada na calibração.

---

## Software Necessário

- **Arduino IDE** (versão 1.8.19 ou superior)
- Bibliotecas (instalar via Gerenciador de Bibliotecas):
  - `Adafruit AHTX0`
  - `DFRobot_ENS160`
  - `PubSubClient` (para MQTT)
  - `ArduinoJson` (versão 6)
  - `TimeLib.h` (já inclusa no core ESP8266)
- Placa `NodeMCU 1.0 (ESP-12E Module)` no gerenciador de placas ESP8266.

---

## Como Compilar e Executar

1. **Clone ou faça o download** deste repositório.
2. **Abra o arquivo** `SIMAA_I.ino` na Arduino IDE.
3. **Configure as credenciais** no topo do código:
   ```cpp
   const char* ssid = "sua_rede";
   const char* password = "sua_senha";
   const char* mqtt_token = "seu_token_thingsboard";

   -------------------------------------------------------------------------------------------------------
Selecione a placa: Ferramentas → Placa → ESP8266 → NodeMCU 1.0 (ESP-12E Module).
Selecione a porta correta e faça o upload.
Abra o Serial Monitor (115200 baud) para ver os dados em tempo real.

** Metodologia de Calibração do Microfone**
O microfone MAX9814 fornece um sinal analógico cuja amplitude (pico a pico – Vpp) é proporcional à intensidade sonora. Para converter Vpp em decibéis (dB SPL), utilizamos uma curva logarítmica, que representa fielmente a percepção humana e a física do som.

** Procedimento de calibração (realizado em campo)**
Silêncio ambiente: Medimos Vpp = 0,1418 V enquanto um decibelímetro de referência (aplicativo de celular calibrado) indicava 53 dB.
Ruído alto (palmas): Medimos Vpp = 2,1914 V com o celular marcando 94 dB.
Resolvemos o sistema:

Equação final de calibração
dB = 34,48 * log10(Vpp) + 82,25

Dados Enviados ao ThingsBoard
O payload JSON enviado a cada 15 segundos contém os seguintes campos:

Campo	      Tipo	  Descrição
timestamp	  string	Data e hora (NTP)
temperatura	float 	Temperatura em °C
umidade	    float 	Umidade relativa em %
eco2	      int	    Dióxido de carbono equivalente (ppm)
tvoc	      int	    Compostos orgânicos voláteis (ppb)
aqi	        int	    Índice de qualidade do ar (1 a 5)
som_max_adc	int	    Valor máximo do ADC durante a janela
som_min_adc	int	    Valor mínimo do ADC durante a janela
som_vpp    	float  	Pico a pico da tensão (V)
som_db	    float  	Nível sonoro calibrado (dB SPL)
