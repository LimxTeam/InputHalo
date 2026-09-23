/*******************************************************************************
 * 文件: SettingsModel.h
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   声明版本一的配置数据模型 SettingsModel，内部用 nlohmann::json 树形保存各分区键值。
 *   提供默认值工厂 Defaults() 与安全合并加载 FromJson()；不涉及复杂算法。
 *
 * 设计哲学:
 *   配置即数据：模型本身不携带行为，仅作为 json 容器，
 *   运行时特性状态留待后续里程碑；防御性校验全部集中在 FromJson。
 *
 * 技术特性:
 *   - values 为通用 JSON 对象，分区/键由默认模板定义
 *   - Defaults() 给出完整基线，作为白名单与回退来源
 *   - FromJson 做类型安全的覆盖合并
 *
 * 依赖关系:
 *   外部: nlohmann/json.hpp
 *
 * 注意事项:
 *   配置版本号固定为 1；项目禁止 STL 容器封装，json 库自带容器。
 ******************************************************************************/
#pragma once
#include <nlohmann/json.hpp>

// InputHalo 配置层命名空间。
namespace ih {
// Version-one configuration data. Runtime feature state belongs to later milestones.
// 版本一配置数据：一份完整的键值树。运行时特性状态后续里程碑再加。
struct SettingsModel {
// 实际配置树，形如 {general:{}, input:{}, cursor:{}, ...}。
    nlohmann::json values;
// 返回内置默认配置(完整基线)。
    static SettingsModel Defaults();
// 从外部 JSON 安全加载：以默认值为模板逐键合并并钳制/校验。
    static SettingsModel FromJson(const nlohmann::json& source);
};
}
