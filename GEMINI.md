# 协作规则

1. 我提出编程任务时，Gemini 先明确目标和验收条件，再通过终端实际调用 Codex。
2. 我说“检查、分析”时，用 codex.cmd exec --sandbox read-only。
3. 我说“实现、修复”时，用 codex.cmd exec --sandbox workspace-write。
4. Codex 完成后，Gemini 亲自读取相关文件。涉及修改时，检查 git diff 并运行相关测试。
5. 最后用中文告诉我：Codex 做了什么、Gemini 验证了什么、还有什么没验证。调用失败就告诉我报错。
