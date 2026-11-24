# CoreBase 官网显卡采集器：AMD 修复版

## 这次如何替换

1. 把当前 `G:\CoreBase\collector.py` 备份成 `collector_before_amd_fix.py`。
2. 用压缩包里的 `collector.py` 替换它。保留原有 `CoreBase.py`、`database.py`、`hardware.db`。
3. 在 PowerShell 运行下面的命令。`collector.py` 必须带 `.py`。

先预览 AMD 数据，不写数据库：

```powershell
cd G:\CoreBase
py collector.py --brand amd --show-browser
```

采集 AMD 并写入现有数据库：

```powershell
py collector.py --brand amd --show-browser --save
```

`--save` 调用你原有的 `database.py`，按 `product_id` 新增或更新记录。它不会清空数据库，也不会删除之前的 NVIDIA、Intel 或示例记录。

其他品牌：

```powershell
py collector.py --brand nvidia --save
py collector.py --brand intel --show-browser --save
```

原有 `py CoreBase.py` 仍然兼容：它会尝试采集三个品牌并保存成功返回的数据。只需采集 AMD 时，使用上面的单品牌命令即可。

## 修复内容

- AMD：读取规格表容器公开的 `data-json` 属性，其中的 `items` 是驱动整张表的数据。已移除点击 Next 的循环，因此不再依赖分页刷新时机。
- 有页面条目计数时，会将它与数据项总数核对。计数尚未渲染时，仍可解析已经到达的完整 JSON 属性；不把当前可见的 25 行当作全部结果。
- 加载：等待主文档响应开始，再等待所需数据，避免无关脚本拖延 `DOMContentLoaded`。暂时性连接错误或导航超时最多重试一次；HTTP 403 等拒绝访问不会反复重试。
- 保留 `--disable-http2`，默认验证 HTTPS 证书。
- 品牌参数不再区分大小写，`--brand AMD` 也可以。
- 新增 `--save`，直接将选定品牌的验证结果存入现有数据库。未加这个参数时只预览。

## 数据范围与限制

- NVIDIA：官网 GeForce 对比页。
- AMD：官网 Graphics Specifications 表中的 Desktop / Component 条目。带 Notebook 的混合条目无法明确区分桌面版本，因此跳过；纯笔记本条目不在采集范围。
- Intel：当前 Arc 产品目录中已列出的桌面产品规格页。
- 官网未提供显存容量、类型或位宽，或版本对应不明确时，跳过并记录原因。不会根据型号猜参数。
- AMD 的字段是 Max Memory Size，只表示表中列出的最大容量，不代表收录了该显卡所有显存版本。
- 此采集器不承诺覆盖全部历史型号、非公版、笔记本、专业卡或 CPU。

`collector_report.json` 会记录各品牌的成功数量、跳过原因与采集错误。`partial` 表示一些条目缺少规格或不明确，`failed` 表示该品牌采集失败。没有任何有效结果时，`--save` 不会调用数据库初始化或写入。

## 已完成的验证

- 实际检查 AMD 官网：HTML 数据属性有 204 个条目；与当前显示的 50 行逐项核对显存容量、类型和位宽，无差异。条目数量可能随官网更新变化。
- 18 项离线回归测试通过，覆盖官网数据样本、缺失字段、重复数据、条目计数、导航重试、NVIDIA / Intel 原有解析及失败时不写库。
- 使用你现有 `database.py`，在临时 SQLite 文件中验证写入与重复更新：没有新增重复记录，已有记录保留。
- 检查了 Python 3.10 语法兼容性，离线测试使用 Python 3.12.2。
- 本次执行环境无法安装 Playwright，因此尚未端到端运行这份 Python 爬虫连接官网。官网数据结构已经实查，但你电脑上的网络、后台浏览器加载仍需用上面的命令验证；不能保证所有网络超时都已消除。

可选：把 `test_collector.py` 也复制到项目目录，运行离线测试：

```powershell
py -m unittest -v test_collector.py
```

## 首次安装时才需要

如果此前已经能运行 Playwright，无需重复安装。新环境需要：

```powershell
py -m pip install -r requirements-official.txt
py -m playwright install chromium
```

## 官方来源

- https://www.nvidia.com/en-us/geforce/graphics-cards/compare/
- https://www.amd.com/en/products/specifications/graphics.html
- https://www.intel.com/content/www/us/en/ark/products/series/227960/intel-arc-dedicated-graphics-family.html
