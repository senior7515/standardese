// Copyright (C) 2016 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#include <standardese/cpp_template.hpp>

#include <cassert>

#include <standardese/detail/parse_utils.hpp>
#include <standardese/detail/tokenizer.hpp>
#include <standardese/cpp_function.hpp>

using namespace standardese;

cpp_ptr<cpp_template_parameter> cpp_template_parameter::try_parse(translation_unit& tu,
                                                                  cpp_cursor        cur,
                                                                  const cpp_entity& parent)
{
    switch (clang_getCursorKind(cur))
    {
    case CXCursor_TemplateTypeParameter:
        return cpp_template_type_parameter::parse(tu, cur, parent);
    case CXCursor_NonTypeTemplateParameter:
        return cpp_non_type_template_parameter::parse(tu, cur, parent);
    case CXCursor_TemplateTemplateParameter:
        return cpp_template_template_parameter::parse(tu, cur, parent);
    default:
        break;
    }

    return nullptr;
}

cpp_ptr<cpp_template_type_parameter> cpp_template_type_parameter::parse(translation_unit& tu,
                                                                        cpp_cursor        cur,
                                                                        const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_TemplateTypeParameter);

    detail::tokenizer tokenizer(tu, cur);
    auto              stream = detail::make_stream(tokenizer);
    auto              name   = detail::parse_name(cur);

    // skip typename
    auto res = detail::skip_if_token(stream, "typename");
    if (!res)
    {
        res = detail::skip_if_token(stream, "class");
        if (!res)
            throw parse_error(source_location(cur),
                              "unexpected token \'" + std::string(stream.peek().get_value().c_str())
                                  + "\'");
    }

    // variadic parameter
    auto is_variadic = false;
    if (stream.peek().get_value() == "...")
    {
        stream.bump();
        detail::skip_whitespace(stream);
        is_variadic = true;
    }

    // skip name
    if (!name.empty())
        detail::skip(stream, cur, {name.c_str()});

    // default
    std::string def_name;
    if (stream.peek().get_value() == "=")
    {
        stream.bump();
        detail::skip_whitespace(stream);

        while (!stream.done())
            def_name += stream.get().get_value().c_str();

        detail::erase_trailing_ws(def_name);
        detail::unmunch(def_name);
    }

    return detail::make_ptr<cpp_template_type_parameter>(cur, parent, cpp_type_ref(def_name, {}),
                                                         is_variadic);
}

cpp_ptr<cpp_non_type_template_parameter> cpp_non_type_template_parameter::parse(
    translation_unit& tu, cpp_cursor cur, const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_NonTypeTemplateParameter);

    detail::tokenizer tokenizer(tu, cur);
    auto              stream = detail::make_stream(tokenizer);
    auto              name   = detail::parse_name(cur);

    // given type
    std::string type_given;
    while (stream.peek().get_value() != "..." && stream.peek().get_value() != name.c_str())
    {
        detail::skip_attribute(stream, cur);
        type_given += stream.get().get_value().c_str();
    }

    // variadic parameter
    auto is_variadic = false;
    if (stream.peek().get_value() == "...")
    {
        stream.bump();
        detail::skip_whitespace(stream);
        is_variadic = true;
    }

    // skip name
    if (!name.empty())
        detail::skip(stream, cur, {name.c_str()});

    // continue with type
    while (!stream.done() && stream.peek().get_value() != "=")
        type_given += stream.get().get_value().c_str();

    detail::erase_trailing_ws(type_given);
    detail::unmunch(type_given);

    // default
    std::string def;
    if (stream.peek().get_value() == "=")
    {
        stream.bump();
        detail::skip_whitespace(stream);

        while (!stream.done())
            def += stream.get().get_value().c_str();

        detail::erase_trailing_ws(def);
        detail::unmunch(def);
    }

    auto type = clang_getCursorType(cur);
    return detail::make_ptr<cpp_non_type_template_parameter>(cur, parent,
                                                             cpp_type_ref(std::move(type_given),
                                                                          type),
                                                             std::move(def), is_variadic);
}

namespace
{
    bool is_template_template_variadic(translation_unit& tu, cpp_cursor cur, const cpp_name& name)
    {
        detail::tokenizer tokenizer(tu, cur);
        auto              stream = detail::make_stream(tokenizer);

        auto found = false;
        while (!stream.done())
        {
            if (detail::skip_if_token(stream, "..."))
                found = true;
            else if (detail::skip_if_token(stream, name.c_str()))
                break;
            else if (!std::isspace(stream.get().get_value()[0]))
                found = false;
        }

        return found;
    }
}

cpp_ptr<cpp_template_template_parameter> cpp_template_template_parameter::parse(
    translation_unit& tu, cpp_cursor cur, const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_TemplateTemplateParameter);

    auto name     = detail::parse_name(cur);
    auto variadic = is_template_template_variadic(tu, cur, name);
    auto result = detail::make_ptr<cpp_template_template_parameter>(cur, parent, cpp_template_ref(),
                                                                    variadic);

    detail::visit_children(cur, [&](CXCursor cur, CXCursor) {
        if (auto param = cpp_template_parameter::try_parse(tu, cur, *result))
            result->add_paramter(std::move(param));
        else
        {
            assert(clang_getCursorKind(cur) == CXCursor_TemplateRef);
            result->default_ = cpp_template_ref(cur, detail::parse_name(cur));
        }

        return CXChildVisit_Continue;
    });

    return result;
}

bool standardese::is_full_specialization(translation_unit& tu, cpp_cursor cur)
{
    detail::tokenizer tokenizer(tu, cur);
    auto              stream = detail::make_stream(tokenizer);

    return stream.get().get_value() == "template";
}

const cpp_function_base* standardese::get_function(const cpp_entity& e) STANDARDESE_NOEXCEPT
{
    if (is_function_like(e.get_entity_type()))
        return static_cast<const cpp_function_base*>(&e);
    else if (e.get_entity_type() == cpp_entity::function_template_t)
        return &static_cast<const cpp_function_template&>(e).get_function();
    else if (e.get_entity_type() == cpp_entity::function_template_specialization_t)
        return &static_cast<const cpp_function_template_specialization&>(e).get_function();
    return nullptr;
}

namespace
{
    template <typename T>
    void parse_parameters(translation_unit& tu, T& result, cpp_cursor cur)
    {
        detail::visit_children(cur, [&](CXCursor cur, CXCursor) {
            if (auto ptr = cpp_template_parameter::try_parse(tu, cur, result))
            {
                result.add_template_parameter(std::move(ptr));
                return CXChildVisit_Continue;
            }
            return CXChildVisit_Break;
        });
    }

    // appends template paramters to name
    template <typename T>
    cpp_name get_template_name(std::string name, T& result)
    {
        name += "<";
        auto needs_comma = false;

        for (auto& param : result.get_template_parameters())
        {
            if (param.get_name().empty())
                continue;

            if (needs_comma)
                name += ", ";
            else
                needs_comma = true;

            name += param.get_name().c_str();
            if (param.is_variadic())
                name += "...";
        }
        name += ">";

        return name;
    }

    // appends specialization arguments to name
    cpp_name get_template_specialization_name(translation_unit& tu, cpp_cursor cur,
                                              const std::string& name)
    {
        if (name.empty())
            return "";

        detail::tokenizer tokenizer(tu, cur);
        auto              stream = detail::make_stream(tokenizer);

        while (stream.get().get_value() != name.c_str())
            ;

        auto result = name + "<";

        auto bracket_count = 1;
        detail::skip(stream, cur, "<");
        while (bracket_count == 1)
        {
            auto spelling = stream.get().get_value();

            if (spelling == "<")
                ++bracket_count;
            else if (spelling == ">")
                --bracket_count;

            result += spelling.c_str();
        }

        return result;
    }
}

cpp_ptr<cpp_function_template> cpp_function_template::parse(translation_unit& tu, cpp_cursor cur,
                                                            const cpp_entity& parent)
{
    auto result = detail::make_ptr<cpp_function_template>(cur, parent);
    parse_parameters(tu, *result, cur);

    auto func = cpp_function_base::try_parse(tu, cur, parent);
    assert(func);
    result->func_ = std::move(func);

    return result;
}

cpp_name cpp_function_template::get_name() const
{
    return get_template_name(func_->get_name().c_str(), *this);
}

cpp_function_template::cpp_function_template(cpp_cursor cur, const cpp_entity& parent)
: cpp_entity(get_entity_type(), cur, parent)
{
}

cpp_ptr<cpp_function_template_specialization> cpp_function_template_specialization::parse(
    translation_unit& tu, cpp_cursor cur, const cpp_entity& parent)
{
    auto result = detail::make_ptr<cpp_function_template_specialization>(cur, parent);

    auto func = cpp_function_base::try_parse(tu, cur, parent);
    assert(func);
    result->func_ = std::move(func);
    result->name_ = get_template_specialization_name(tu, cur, result->func_->get_name().c_str());

    auto primary_cur = clang_getSpecializedCursorTemplate(cur);
    assert(primary_cur != cpp_cursor());
    result->primary_ = cpp_template_ref(primary_cur, result->func_->get_name());

    return result;
}

cpp_function_template_specialization::cpp_function_template_specialization(cpp_cursor        cur,
                                                                           const cpp_entity& parent)
: cpp_entity(get_entity_type(), cur, parent), name_("")
{
}

cpp_ptr<cpp_class_template> cpp_class_template::parse(translation_unit& tu, cpp_cursor cur,
                                                      const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_ClassTemplate);

    auto result = detail::make_ptr<cpp_class_template>(cur, parent);
    parse_parameters(tu, *result, cur);

    auto ptr = cpp_class::parse(tu, cur, parent);
    if (!ptr)
        return nullptr;
    result->class_ = std::move(ptr);

    return result;
}

cpp_name cpp_class_template::get_name() const
{
    return get_template_name(class_->get_name().c_str(), *this);
}

cpp_ptr<cpp_class_template_full_specialization> cpp_class_template_full_specialization::parse(
    translation_unit& tu, cpp_cursor cur, const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_ClassDecl
           || clang_getCursorKind(cur) == CXCursor_StructDecl
           || clang_getCursorKind(cur) == CXCursor_UnionDecl);

    auto result = detail::make_ptr<cpp_class_template_full_specialization>(cur, parent);

    auto ptr = cpp_class::parse(tu, cur, parent);
    if (!ptr)
        return nullptr;
    result->class_ = std::move(ptr);
    result->name_  = get_template_specialization_name(tu, cur, result->class_->get_name().c_str());

    auto primary_cur = clang_getSpecializedCursorTemplate(cur);
    assert(primary_cur != cpp_cursor());
    result->primary_ = cpp_template_ref(primary_cur, result->class_->get_name());

    return result;
}

cpp_ptr<cpp_class_template_partial_specialization> cpp_class_template_partial_specialization::parse(
    translation_unit& tu, cpp_cursor cur, const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_ClassTemplatePartialSpecialization);

    auto result = detail::make_ptr<cpp_class_template_partial_specialization>(cur, parent);
    parse_parameters(tu, *result, cur);

    auto ptr = cpp_class::parse(tu, cur, parent);
    if (!ptr)
        return nullptr;
    result->class_ = std::move(ptr);
    result->name_  = get_template_specialization_name(tu, cur, result->class_->get_name().c_str());

    auto primary_cur = clang_getSpecializedCursorTemplate(cur);
    assert(primary_cur != cpp_cursor());
    result->primary_ = cpp_template_ref(primary_cur, result->class_->get_name());

    return result;
}

const cpp_class* standardese::get_class(const cpp_entity& e) STANDARDESE_NOEXCEPT
{
    if (e.get_entity_type() == cpp_entity::class_t)
        return static_cast<const cpp_class*>(&e);
    else if (e.get_entity_type() == cpp_entity::class_template_t)
        return &static_cast<const cpp_class_template&>(e).get_class();
    else if (e.get_entity_type() == cpp_entity::class_template_full_specialization_t)
        return &static_cast<const cpp_class_template_full_specialization&>(e).get_class();
    else if (e.get_entity_type() == cpp_entity::class_template_partial_specialization_t)
        return &static_cast<const cpp_class_template_partial_specialization&>(e).get_class();
    return nullptr;
}

#if CINDEX_VERSION_MINOR >= 32
cpp_ptr<cpp_alias_template> cpp_alias_template::parse(translation_unit& tu, cpp_cursor cur,
                                                      const cpp_entity& parent)
{
    assert(clang_getCursorKind(cur) == CXCursor_TypeAliasTemplateDecl);

    auto result = detail::make_ptr<cpp_alias_template>(cur, parent);
    parse_parameters(tu, *result, cur);

    cpp_cursor type;
    detail::visit_children(cur, [&](cpp_cursor cur, cpp_cursor) {
        if (clang_getCursorKind(cur) == CXCursor_TypeAliasDecl)
        {
            type = cur;
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    });

    result->type_ = cpp_type_alias::parse(tu, type, parent);
    assert(result->type_);

    return result;
}
#endif

cpp_name cpp_alias_template::get_name() const
{
    return get_template_name(type_->get_name().c_str(), *this);
}
