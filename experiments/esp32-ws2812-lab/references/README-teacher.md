<h1>实验1：WS2812 RGB灯闪烁 + 呼吸灯</h1>
<h2>功能</h2>
<p>| 模式 | 说明 |
|------|------|
| 基础 | WS2812红灯每500ms翻转（1秒周期），串口打印LED状态 |
| 进阶 | RGB颜色渐变（红→绿→蓝循环），实现呼吸灯效果 |</p>
<h2>引脚（关键！）</h2>
<p>| 外设 | 引脚 | 说明 |
|------|------|------|
| WS2812 RGB灯 | <strong>GPIO21</strong> | 板载SPI灯条数据线（来自HCS配置注释）|</p>
<blockquote>
<p>⚠️ <strong>实训箱的 WS2812 走 SPI 总线，不是直接 GPIO</strong>。</p>
<ul>
<li>数据线接在 ESP32 的 <strong>GPIO23（VSPI_MOSI）</strong></li>
<li>需要用 SPI 发送特定字节来模拟 WS2812 时序</li>
<li>这是实训箱源码 <code>ws2812_drive.c</code> 的实现方式</li>
</ul>
</blockquote>
<h2>WS2812 SPI 驱动原理</h2>
<p>WS2812 采用 NZR（Non-Return-to-Zero）单总线协议，每个 bit 的高电平宽度决定 0/1。实训箱利用 SPI 字节来精确生成这个时序：</p>
<pre><code>SPI时钟 = 6.4MHz → 每bit = 156.25ns → 8bit字节 = 1.25us（正好是WS2812一个位周期）

WS2812 &#34;1&#34;码 = 高电平 800ns + 低电平 450ns
  → 0xFC (11111100) = 高6bit + 低2bit = 937.5ns高 + 312.5ns低 ≈ 接近&#34;1&#34;码

WS2812 &#34;0&#34;码 = 高电平 400ns + 低电平 850ns
  → 0xE0 (11100000) = 高3bit + 低5bit = 468.75ns高 + 781.25ns低 ≈ 接近&#34;0&#34;码
</code></pre>
<p>数据格式：<strong>GRB</strong> 顺序，每色 8bit。</p>
<h2>编译运行</h2>
<pre><code>cd exp1_blink
idf.py set-target esp32
idf.py build
idf.py flash monitor
</code></pre>
<h2>模式切换</h2>
<p>在 <code>ma