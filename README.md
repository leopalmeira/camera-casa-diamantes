# ESP32-CAM — Monitoramento remoto e alertas WhatsApp

Protótipo: ESP32-CAM AI Thinker + Node.js + Baileys + Sharp. **Use somente em espaços onde você tem autorização para monitorar e informe os envolvidos.** Sem áudio e sem gravação histórica: mantém apenas o quadro atual em RAM. A câmera envia JPEG (~1 fps), **não** fornece vídeo HD fluido. A análise de movimento é realizada **no servidor**, não no microcontrolador.

## 1. Requisitos

- ESP32-CAM **AI Thinker** com câmera OV2640 e fonte **5 V estável** (conforme a placa; GPIO é 3,3 V), adaptador USB-serial para gravar.
- Arduino IDE 2.x, placa instalada via **Boards Manager: esp32 by Espressif Systems**, selecionar **AI Thinker ESP32-CAM**, habilitar PSRAM quando aplicável.
- Node.js 20+ no servidor; rede Wi-Fi **2,4 GHz** com internet, sem portal cativo.
- Hospedagem Node.js **sempre ativa**, que suporte conexões HTTP prolongadas e **disco persistente** para sessão do WhatsApp e contatos. GitHub é repositório, não backend. GitHub Pages sozinho não serve para isso.

## 2. Servidor (primeiro)

```bash
cd server
cp .env.example .env
# Editar .env antes de iniciar; Node não carrega .env automaticamente:
# no Node 20.6+ use o comando abaixo:
node --env-file=.env index.js
```

Para instalar dependências:

```bash
npm install
node --env-file=.env index.js
```

Produção: hospede a pasta `server` como serviço web Node; comando `npm install` e `npm start`; configure as variáveis de ambiente no painel do provedor. **PUBLIC_URL** deve apontar para a URL HTTPS real. Configure um **disco persistente montado** no caminho de `DATA_DIR` (por exemplo `/data`); sem isso, ao reiniciar o serviço você pode perder a sessão do WhatsApp e os contatos. Não exponha `.env` nem `data/` no GitHub.

- `ADMIN_USER`, `ADMIN_PASSWORD`: login do painel via autenticação HTTP Basic (HTTPS obrigatório).
- `DEVICE_TOKEN`: chave aleatória de pelo menos 32 caracteres; coloque também no ESP32.
- `PUBLIC_URL`: URL externa HTTPS, por exemplo `https://camera.exemplo.com`.
- `DATA_DIR`: caminho com persistência permanente.
- `ALERT_COOLDOWN_SECONDS`: tempo mínimo entre disparos de alertas, padrão 120 segundos.
- `MOTION_THRESHOLD`: proporção de pixels alterados, padrão 0.13; ajustar para o ambiente.
- `PIXEL_THRESHOLD`: diferença mínima de brilho por pixel, padrão 32.

Abra `https://seu-servidor/` e autentique. O site exibirá um QR Code gerado pelo Baileys. Leia com o **WhatsApp do telefone que enviará os avisos**, em **Dispositivos conectados > Conectar dispositivo**. Cadastre destinatários, um por linha, com DDI, ex.: `5521999999999`.

**Baileys não é API oficial do WhatsApp.** Conexões podem falhar, a conta pode sofrer restrições e a entrega não é garantida. Para operação comercial crítica, considere a API oficial WhatsApp Business/Cloud API, respeitando seus requisitos e políticas. Não use para spam.

## 3. ESP32-CAM Arduino IDE

1. Abra `firmware/ESP32CAM_Seguranca/ESP32CAM_Seguranca.ino`.
2. Selecione **AI Thinker ESP32-CAM** e a porta serial. Para gravação por USB-serial, conectar GPIO0 ao GND, iniciar upload, pulsar RST quando solicitado; remover a ligação GPIO0–GND após upload e reiniciar.
3. Na primeira inicialização, o ESP32 abre a rede `CAM-CONFIG-...` (senha mostrada no **Monitor Serial 115200 baud**; é `CAM-` + parte do identificador do chip, sem caracteres adicionais, salvo se muito curto).
4. Conecte o celular à rede `CAM-CONFIG-...` e abra `http://192.168.4.1` (se celular avisar que não há internet, mantenha a conexão).
5. Cadastre SSID/senha da rede de casa, segunda rede (opcional), URL **HTTPS** do servidor e `DEVICE_TOKEN` exatamente igual ao do servidor. Salve.
6. O ESP32 reinicia, tenta as redes em sequência, e envia imagens a `/api/frame` com autenticação Bearer. Em caso de perda de conexão, tenta novamente a cada ~15 segundos, alternando as redes disponíveis. O painel mostra a câmera online quando recebe quadros recentes.
7. Para **reconfigurar** depois, com o módulo desligado, conecte **GPIO13 ao GND**, ligue e desconecte o jumper após a abertura do portal. **Não usar microSD** com esta configuração.

A **rede Claro** funciona se for um SSID Wi-Fi normal de 2,4 GHz cuja senha você conhece e tem permissão para usar. Redes públicas com página de login/captive portal não funcionam com este firmware sem desenvolvimento adicional. ESP32-CAM comum não suporta rede celular diretamente (4G/5G); precisará de roteador/hotspot externo.

## 4. Observações técnicas e de segurança

- Firmware atual usa `WiFiClientSecure::setInsecure()` **somente para facilitar o teste**, o que deixa o ESP32 vulnerável a ataques intermediários na rede. **Antes de produção, substituir por validação TLS com CA raiz confiável**, atualizando certificados conforme o provedor.
- Autenticação HTTP Basic só deve ser usada com HTTPS e senha longa. Para múltiplos usuários/produção, adotar login com sessões e proteção contra força bruta.
- O portal AP usa senha derivada do identificador do chip e deve ser usado apenas em local controlado. Para produção, configure senha única aleatória por dispositivo, maior controle de acesso e um mecanismo de reset protegido.
- Queda/hibernação do servidor interrompe vídeo e alertas. Um serviço gratuito que dorme **não atende** monitoramento 24/7.
- O algoritmo compara pixels em 32×24; sujeita-se a falso positivo por luz/sombra. Começa após quadros de aquecimento; só alerta dentro da cadência de cooldown; **não substitui um alarme profissional**. Ajuste sensibilidade conforme o ambiente.
- Configuração de até 15 números (todos devem aceitar os alertas). Sem reconhecimento facial, sem gravação de histórico, sem armazenamento do vídeo.
- O servidor mantém clientes MJPEG conectados em memória. Para muitos usuários, prefira distribuição WebRTC/HLS com infraestrutura dedicada.

## 5. GitHub

Crie um repositório privado e suba somente `firmware/`, `server/`, `.gitignore`, `README.md`. **Nunca** envie os valores reais de senhas, tokens ou `server/data/` para o GitHub.

## 6. Diagnóstico

- Sem imagens: `Monitor Serial` em 115200; verifique alimentação 5 V, seleção de placa, Wi-Fi e URL.
- HTTP 401 no ESP32: `DEVICE_TOKEN` incorreto.
- HTTP 413: quadro grande demais; mantenha QVGA.
- Sem QR Code: verifique logs do servidor e permissões do diretório persistente.
- WhatsApp desconecta: verifique rede, sessão gravada e versão compatível da biblioteca.
- A tela fica preta: primeira transmissão pode demorar; abrir `/health` deve retornar `{"ok":true}` sem autenticação.
