#include "LuaBindings.h"
#include "scene/Scene.h"
#include "scene/SceneActivity.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
constexpr const char *kObjectType = "Engine.ScriptObject";
enum class Op {
	TEXT,
	SET_TEXT,
	POSITION,
	SET_POSITION,
	SIZE,
	SET_SIZE,
	TEXT_COLOR,
	SET_TEXT_COLOR,
	IMAGE_COLOR,
	SET_IMAGE_COLOR,
	INTERACTABLE,
	SET_INTERACTABLE,
	IMAGE_FILL,
	SET_IMAGE_FILL
};

/// <summary>
/// Lua APIを使わず、UIの値を問い合わせ・変更する。所有するC++値はここで破棄する
/// </summary>
/// <param name="call">作業シーンと文字列の保持先</param>
/// <param name="op">処理種別</param>
/// <param name="id">対象物体ID</param>
/// <param name="text">Luaが保持する入力文字列。文字列変更以外はnullptr</param>
/// <param name="length">入力文字列のバイト数</param>
/// <param name="numbers">有限な入力・出力数値の4成分</param>
/// <returns>true:成功 false:call.errorへ失敗理由を設定</returns>
bool Perform(LuaInternal::Call &call, Op op, SceneObjectId id, const char *text,
             std::size_t length, float *numbers) noexcept {
	try {
		const bool mutation = op == Op::SET_TEXT || op == Op::SET_POSITION ||
		                      op == Op::SET_SIZE || op == Op::SET_TEXT_COLOR ||
		                      op == Op::SET_IMAGE_COLOR ||
		                      op == Op::SET_INTERACTABLE ||
		                      op == Op::SET_IMAGE_FILL;
		if (!call.scene || (mutation && !call.executing))
			throw std::runtime_error(
			    "Change UI values inside callbacks, not at module scope.");
		auto *object = call.scene->FindObject(id);
		if (!object)
			throw std::runtime_error("The UI object no longer exists.");
		if (op == Op::INTERACTABLE || op == Op::SET_INTERACTABLE) {
			if (!object->button)
				throw std::runtime_error(
				    "This object needs a UI Button component.");
			if (op == Op::INTERACTABLE)
				numbers[0] = object->button->enabled &&
				                     object->button->interactable &&
				                     SceneActivity::IsActive(*call.scene, id)
				                 ? 1.0f
				                 : 0.0f;
			else
				object->button->interactable = numbers[0] != 0;
		} else if (op == Op::TEXT || op == Op::SET_TEXT ||
		           op == Op::TEXT_COLOR || op == Op::SET_TEXT_COLOR) {
			if (!object->text)
				throw std::runtime_error(
				    "This object needs a UI Text component.");
			if (op == Op::TEXT)
				call.returnText = object->text->text;
			else if (op == Op::TEXT_COLOR)
				std::copy(object->text->color.begin(),
				          object->text->color.end(), numbers);
			else {
				auto value = *object->text;

				if (op == Op::SET_TEXT) {
					if (length > TextComponent::kMaximumTextBytes)
						throw std::runtime_error(
						    "UI text exceeds 4096 UTF-8 bytes.");
					value.text.assign(text, length);
				} else
					std::copy(numbers, numbers + 4, value.color.begin());
				if (!value.IsValid())
					throw std::runtime_error(
					    "UI text or RGBA color is invalid.");
				object->text = std::move(value);
			}
		} else if (op == Op::IMAGE_FILL || op == Op::SET_IMAGE_FILL) {
			if (!object->image)
				throw std::runtime_error(
				    "This object needs a UI Image component.");
			if (op == Op::IMAGE_FILL)
				numbers[0] = object->image->fillAmount;
			else {
				if (numbers[0] < 0 || numbers[0] > 1)
					throw std::runtime_error(
					    "UI image fill must be between 0 and 1.");
				object->image->fillAmount = numbers[0];
			}
		} else if (op == Op::IMAGE_COLOR || op == Op::SET_IMAGE_COLOR) {
			if (!object->image)
				throw std::runtime_error(
				    "This object needs a UI Image component.");
			if (op == Op::IMAGE_COLOR)
				std::copy(object->image->color.begin(),
				          object->image->color.end(), numbers);
			else {
				auto value = *object->image;
				std::copy(numbers, numbers + 4, value.color.begin());
				if (!value.IsValid())
					throw std::runtime_error("UI image color is invalid.");
				object->image = std::move(value);
			}
		} else {
			if (!object->rectTransform)
				throw std::runtime_error("This object needs Rect Transform.");
			auto value = *object->rectTransform;
			auto &field = (op == Op::POSITION || op == Op::SET_POSITION)
			                  ? value.position
			                  : value.sizeDelta;
			if (op == Op::POSITION || op == Op::SIZE)
				std::copy(field.begin(), field.end(), numbers);
			else {
				std::copy(numbers, numbers + 2, field.begin());
				if (!value.IsValid())
					throw std::runtime_error(
					    "UI rectangle values are invalid.");
				object->rectTransform = value;
			}
		}

		return true;
	} catch (const std::exception &exception) {
		call.error.assign(
		    exception.what(),
		    (std::min)(std::strlen(exception.what()), std::size_t(512)));
		return false;
	} catch (...) {
		call.error.assign("UI API failed.");
		return false;
	}
}

/// <summary>
/// 所有値を持たないフレームから、Luaの引数と結果をUI APIへ接続する
/// </summary>
/// <param name="state">保護呼び出し内のLua状態</param>
/// <returns>返した値の数。型・処理の失敗はLuaエラー</returns>
int Dispatch(lua_State *state) {
	const auto op = static_cast<Op>(lua_tointeger(state, lua_upvalueindex(1)));
	auto &call = **static_cast<LuaInternal::Call **>(lua_getextraspace(state));
	const auto id =
	    *static_cast<SceneObjectId *>(luaL_checkudata(state, 1, kObjectType));
	float numbers[4]{};
	const char *text = nullptr;
	std::size_t length = 0;

	if (op == Op::SET_INTERACTABLE) {
		luaL_checktype(state, 2, LUA_TBOOLEAN);
		numbers[0] = lua_toboolean(state, 2) ? 1.0f : 0.0f;
	}

	if (op == Op::SET_TEXT) {
		luaL_checktype(state, 2, LUA_TSTRING);
		text = lua_tolstring(state, 2, &length);
	}

	if (op == Op::SET_POSITION || op == Op::SET_SIZE ||
	    op == Op::SET_TEXT_COLOR || op == Op::SET_IMAGE_COLOR ||
	    op == Op::SET_IMAGE_FILL) {
		const int count = op == Op::SET_IMAGE_FILL                         ? 1
		                  : (op == Op::SET_POSITION || op == Op::SET_SIZE) ? 2
		                                                                   : 4;
		for (int i = 0; i < count; ++i) {
			luaL_checktype(state, i + 2, LUA_TNUMBER);
			const double value = lua_tonumber(state, i + 2);
			if (!std::isfinite(value) ||
			    std::abs(value) > (std::numeric_limits<float>::max)())
				return luaL_error(state, "UI values must be finite floats.");
			numbers[i] = static_cast<float>(value);
		}
	}
	if (!Perform(call, op, id, text, length, numbers)) {
		lua_pushlstring(state, call.error.data(), call.error.size());
		return lua_error(state);
	}

	if (op == Op::INTERACTABLE) {
		lua_pushboolean(state, numbers[0] != 0);
		return 1;
	}

	if (op == Op::TEXT) {
		lua_pushlstring(state, call.returnText.data(), call.returnText.size());
		return 1;
	}

	const int result = op == Op::IMAGE_FILL                              ? 1
	                   : (op == Op::POSITION || op == Op::SIZE)          ? 2
	                   : (op == Op::TEXT_COLOR || op == Op::IMAGE_COLOR) ? 4
	                                                                     : 0;
	for (int i = 0; i < result; ++i)
		lua_pushnumber(state, numbers[i]);
	return result;
}
} // namespace

void LuaInternal::AddUIMethods(lua_State *state) {
	const char *names[]{
	    "get_text",        "set_text",        "get_ui_position",
	    "set_ui_position", "get_ui_size",     "set_ui_size",
	    "get_text_color",  "set_text_color",  "get_image_color",
	    "set_image_color", "is_interactable", "set_interactable",
	    "get_image_fill",  "set_image_fill"};
	for (int i = 0; i < static_cast<int>(std::size(names)); ++i) {
		lua_pushinteger(state, i);
		lua_pushcclosure(state, Dispatch, 1);
		lua_setfield(state, -2, names[i]);
	}
}
