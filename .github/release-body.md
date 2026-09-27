一张图铺满桌面，鼠标一动，整张图反向平移；鼠标回到屏幕中心，画面回到正中。
纯 Win32 API + GDI+，**单文件源码、零第三方依赖**。

## 下载

| 文件 | 说明 |
| --- | --- |
| `__ZIP_NAME__` | 内含 `MovingBackground.exe` + 默认图 `bg.jpg`，解压即用 |

**系统要求**：Windows 10 / 11（64 位）。已在 Windows 11 24H2、2560×1600 / 150% 缩放下实测通过。

本版二进制由 **GitHub Actions 从本标签对应的源码自动编译**，任何人都可以在本仓库的
Actions 页面查看完整构建日志 —— 源码和二进制天然一致，不需要信任发布者。

> ### ⚠️ 首次运行会弹「Windows 已保护你的电脑」
>
> 这是**正常的**：个人开发者发布、没有购买代码签名证书的程序都会这样。
> 点「更多信息」→「仍要运行」即可。
>
> 不放心的话可以自己编译一份 —— 源码只有一个 `MovingBackground.cpp`，
> 双击仓库里的 `build.bat` 就能出一份完全一样的程序。
>
> **不想要预编译版本的话，直接忽略这个 zip，自己编译即可。**

### 校验值（SHA256）

```
MovingBackground.exe      : __EXE_SHA256__
__ZIP_NAME__  : __ZIP_SHA256__
```

## 快速上手

1. 解压 zip（**`bg.jpg` 必须和 exe 放在同一目录**，程序默认加载它）
2. 双击 `MovingBackground.exe` —— 图片会盖在桌面上，跟着鼠标反向平移
3. 想换自己的图：托盘图标**右键** →「打开设置」→ 选图片
4. 退出：托盘右键 →「退出」，或 `Ctrl+Alt+Q`

## 这一版有什么

- **单文件、零依赖** —— 全部代码在 `MovingBackground.cpp`，`build.bat` 一键编译（MSVC / MinGW 均可）
- **弹簧缓冲**：加减速自然，不是生硬跟随
- **边缘永不露黑边**：位移被硬夹在可移动余量之内
- **帧率档位** 30 / 60 / 75 / 90 / 120 / 144 / 180 / 240，按 QPC 绝对时间调度（不是 `WM_TIMER`）
- **中心死区 + 阻尼比可调** —— 专门给容易晕的人留的开关，README 里有「怕晕怎么调」的完整顺序
- **per-monitor-v2 DPI 感知**：150% 缩放下按物理像素渲染，不会被系统拉伸变糊
- **托盘管理 + 单实例**，设置改完立即生效，不用重启
- **开机自启默认关闭** —— 程序不会自作主张改你的注册表，想要得自己去托盘里勾

## 已知限制

- **运行期间桌面图标会被图片盖住**（图标仍在原位、仍点得到，只是看不见）。
  这是 Windows 11 24H2 之后唯一能让图片「既显示、鼠标又能穿透」的挂法 ——
  网上流传的 `WorkerW` 壁纸层做法在新版 Windows 上**完全不会被渲染**。
  四种挂法的实测对比见 README「关于层级」。
- 想让它盖住所有程序，在设置里勾「置顶（盖住所有程序）」。
- PNG 的透明区域按黑色处理。

## 许可证

[MIT](LICENSE) —— 随便用、随便改、可以商用，保留版权声明即可。

<details>
<summary><b>English</b></summary>

An image covers your entire desktop, and **the whole image pans in the opposite
direction as you move the mouse**. Move the cursor back to the screen center and the
image recenters. Written in plain **Win32 API + GDI+** — single-file source, zero
third-party dependencies.

**Requirements**: Windows 10 / 11 (64-bit). Tested on Windows 11 24H2 at 2560×1600 / 150% scaling.

**Download**: `__ZIP_NAME__` — contains `MovingBackground.exe` plus the default image `bg.jpg`.

This binary is **built automatically by GitHub Actions from the source at this tag**.
The full build log is public in the Actions tab, so the binary provably matches the source —
no need to trust the publisher.

> **Windows SmartScreen will warn you on first run** — expected for an unsigned
> binary from an individual developer. Click *More info* → *Run anyway*.
> If you would rather not trust a prebuilt binary, run `build.bat` and compile it yourself.

**SHA256**

```
MovingBackground.exe      : __EXE_SHA256__
__ZIP_NAME__  : __ZIP_SHA256__
```

**Quick start**: unzip (keep `bg.jpg` next to the exe), run `MovingBackground.exe`,
then right-click the tray icon to open Settings and pick your own image.
Quit via the tray menu or `Ctrl+Alt+Q`.

**Highlights**: spring-damped motion · hard edge clamping (never shows black bars) ·
30–240 fps presets scheduled against QPC · adjustable dead zone and damping ratio for
motion sensitivity · per-monitor-v2 DPI aware · tray icon + settings dialog with live
apply · autostart **off** by default (the program never touches your registry on its own).

**Known limitation**: desktop icons are hidden while it runs (still in place and
clickable). On Windows 11 24H2+ this is the only approach that both renders *and* stays
mouse-transparent — the widely-copied `WorkerW` wallpaper-layer trick no longer renders
at all. See the "关于层级" section of the README for the measured comparison.

**License**: MIT.

</details>
