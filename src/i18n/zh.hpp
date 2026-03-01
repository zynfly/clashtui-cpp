#pragma once

// Chinese string table - included by i18n.hpp after Strings is defined

inline constexpr Strings ZH_STRINGS_DEF = {
    // General
    "clashtui-cpp",
    "已连接",
    "未连接",
    "确认",
    "取消",

    // Mode
    "全局",
    "规则",
    "直连",

    // Proxy panel
    "代理组",
    "节点",
    "详情",
    "切换节点中...",
    "测试延迟中...",
    "测试全部",

    // Subscription
    "订阅管理",
    "添加",
    "更新",
    "删除",
    "最后更新",
    "下载中...",
    "成功",
    "失败",

    // Install wizard
    "安装向导",
    "未找到 Mihomo",
    "下载中...",
    "验证中...",
    "安装完成",
    "选择安装路径",

    // Install wizard (extended)
    "Mihomo 已安装",
    "检查安装状态...",
    "获取最新版本信息...",
    "准备安装",
    "解压中...",
    "安装中...",
    "需要 sudo 权限",
    "用户安装 (无需 sudo)",
    "检查更新",
    "已是最新版本",
    "有新版本可用",
    "升级",
    "按回车下载安装",
    "尝试直接下载...",
    "尝试镜像下载...",
    "校验通过",
    "校验失败！",
    "无法获取校验信息",
    "平台",
    "未找到兼容的安装包",

    // Uninstall
    "卸载 Mihomo",
    "确认卸载？",
    "同时删除配置文件？",
    "停止服务...",
    "禁用服务...",
    "删除服务文件...",
    "删除程序文件...",
    "删除配置文件...",
    "卸载完成",
    "卸载失败",

    // Systemd service
    "Systemd 服务配置",
    "创建 systemd 服务？",
    "服务已创建并启动",
    "跳过服务配置",
    "系统服务",
    "用户服务",
    "运行中",
    "未运行",

    // Log panel
    "冻结",
    "解冻",
    "导出",

    // Errors
    "API 请求失败",
    "下载失败",
    "配置无效",
    "节点切换失败",

    // Daemon
    "守护进程运行中",
    "守护进程未运行",
    "启动守护进程...",
    "停止守护进程...",

    // Profile
    "(活跃)",
    "切换配置",
    "配置已切换",
    "启动守护进程以自动更新",
    "更新配置中...",
    "更新全部配置...",
    "无配置文件",

    // Mihomo process
    "启动 mihomo...",
    "停止 mihomo...",
    "重启 mihomo...",
    "mihomo 异常退出",

    // Daemon service
    "clashtui-cpp 守护进程",

    // Service management (extended)
    "启动服务",
    "停止服务",
    "安装服务",
    "卸载服务",
    "服务已启动",
    "服务已停止",
    "服务已卸载",
    "服务未安装",

    // Self-uninstall
    "卸载 clashtui-cpp",
    "从系统中移除 clashtui-cpp？",
    "同时删除配置目录？",
    "clashtui-cpp 已卸载",

    // Self version / update check
    "clashtui-cpp",
    "已是最新",
    "有更新可用",
    "检查更新中...",
};
