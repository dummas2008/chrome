{% extends 'interface_base.cpp' %}


{##############################################################################}
{% block indexed_property_getter %}
{% if indexed_property_getter and not indexed_property_getter.is_custom %}
{% set getter = indexed_property_getter %}
static void indexedPropertyGetter(uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    {% if getter.is_raises_exception %}
    ExceptionState exceptionState(ExceptionState::IndexedGetterContext, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% endif %}
    {% set getter_name = getter.name or 'anonymousIndexedGetter' %}
    {% set getter_arguments = ['index'] %}
    {% if getter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% set getter_arguments = ['scriptState'] + getter_arguments %}
    {% endif %}
    {% if getter.is_raises_exception %}
    {% set getter_arguments = getter_arguments + ['exceptionState'] %}
    {% endif %}
    {{getter.cpp_type}} result = impl->{{getter_name}}({{getter_arguments | join(', ')}});
    {% if getter.is_raises_exception %}
    if (exceptionState.throwIfNeeded())
        return;
    {% endif %}
    if ({{getter.is_null_expression}})
        return;
    {{getter.v8_set_return_value}};
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block indexed_property_getter_callback %}
{% if indexed_property_getter %}
{% set getter = indexed_property_getter %}
static void indexedPropertyGetterCallback(uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    {% if getter.is_custom %}
    {{v8_class}}::indexedPropertyGetterCustom(index, info);
    {% else %}
    {{cpp_class}}V8Internal::indexedPropertyGetter(index, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block indexed_property_setter %}
{% from 'utilities.cpp' import v8_value_to_local_cpp_value %}
{% if indexed_property_setter and not indexed_property_setter.is_custom %}
{% set setter = indexed_property_setter %}
static void indexedPropertySetter(uint32_t index, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    {{v8_value_to_local_cpp_value(setter) | indent}}
    {% if setter.has_exception_state %}
    ExceptionState exceptionState(ExceptionState::IndexedSetterContext, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% endif %}
    {% if setter.has_type_checking_interface %}
    {# Type checking for interface types (if interface not implemented, throw
       TypeError), per http://www.w3.org/TR/WebIDL/#es-interface #}
    if (!propertyValue{% if setter.is_nullable %} && !isUndefinedOrNull(v8Value){% endif %}) {
        exceptionState.throwTypeError("The provided value is not of type '{{setter.idl_type}}'.");
        exceptionState.throwIfNeeded();
        return;
    }
    {% endif %}
    {% set setter_name = setter.name or 'anonymousIndexedSetter' %}
    {% set setter_arguments = ['index', 'propertyValue'] %}
    {% if setter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% set setter_arguments = ['scriptState'] + setter_arguments %}
    {% endif %}
    {% if setter.is_raises_exception %}
    {% set setter_arguments = setter_arguments + ['exceptionState'] %}
    {% endif %}
    bool result = impl->{{setter_name}}({{setter_arguments | join(', ')}});
    {% if setter.is_raises_exception %}
    if (exceptionState.throwIfNeeded())
        return;
    {% endif %}
    if (!result)
        return;
    v8SetReturnValue(info, v8Value);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block indexed_property_setter_callback %}
{% if indexed_property_setter %}
{% set setter = indexed_property_setter %}
static void indexedPropertySetterCallback(uint32_t index, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    {% if setter.is_custom %}
    {{v8_class}}::indexedPropertySetterCustom(index, v8Value, info);
    {% else %}
    {{cpp_class}}V8Internal::indexedPropertySetter(index, v8Value, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block indexed_property_deleter %}
{% if indexed_property_deleter and not indexed_property_deleter.is_custom %}
{% set deleter = indexed_property_deleter %}
static void indexedPropertyDeleter(uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& info)
{
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    {% if deleter.is_raises_exception %}
    ExceptionState exceptionState(ExceptionState::IndexedDeletionContext, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% endif %}
    {% set deleter_name = deleter.name or 'anonymousIndexedDeleter' %}
    {% set deleter_arguments = ['index'] %}
    {% if deleter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% set deleter_arguments = ['scriptState'] + deleter_arguments %}
    {% endif %}
    {% if deleter.is_raises_exception %}
    {% set deleter_arguments = deleter_arguments + ['exceptionState'] %}
    {% endif %}
    DeleteResult result = impl->{{deleter_name}}({{deleter_arguments | join(', ')}});
    {% if deleter.is_raises_exception %}
    if (exceptionState.throwIfNeeded())
        return;
    {% endif %}
    if (result != DeleteUnknownProperty)
        return v8SetReturnValueBool(info, result == DeleteSuccess);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block indexed_property_deleter_callback %}
{% if indexed_property_deleter %}
{% set deleter = indexed_property_deleter %}
static void indexedPropertyDeleterCallback(uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& info)
{
    {% if deleter.is_custom %}
    {{v8_class}}::indexedPropertyDeleterCustom(index, info);
    {% else %}
    {{cpp_class}}V8Internal::indexedPropertyDeleter(index, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_getter %}
{% if named_property_getter and not named_property_getter.is_custom %}
{% set getter = named_property_getter %}
static void namedPropertyGetter(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    auto nameString = name.As<v8::String>();
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    AtomicString propertyName = toCoreAtomicString(nameString);
    {% if getter.is_raises_exception %}
    v8::String::Utf8Value namedProperty(nameString);
    ExceptionState exceptionState(ExceptionState::GetterContext, *namedProperty, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% endif %}
    {% if getter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% endif %}
    {% if getter.use_output_parameter_for_result %}
    {{getter.cpp_type}} result;
    {{getter.cpp_value}};
    {% else %}
    {{getter.cpp_type}} result = {{getter.cpp_value}};
    {% endif %}
    {% if getter.is_raises_exception %}
    if (exceptionState.throwIfNeeded())
        return;
    {% endif %}
    if ({{getter.is_null_expression}})
        return;
    {{getter.v8_set_return_value}};
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_getter_callback %}
{% if named_property_getter %}
{% set getter = named_property_getter %}
static void namedPropertyGetterCallback(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    {% if getter.is_custom %}
    {{v8_class}}::namedPropertyGetterCustom(name, info);
    {% else %}
    {{cpp_class}}V8Internal::namedPropertyGetter(name, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_setter %}
{% from 'utilities.cpp' import v8_value_to_local_cpp_value %}
{% if named_property_setter and not named_property_setter.is_custom %}
{% set setter = named_property_setter %}
static void namedPropertySetter(v8::Local<v8::Name> name, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    auto nameString = name.As<v8::String>();
    {% if setter.has_exception_state %}
    v8::String::Utf8Value namedProperty(nameString);
    ExceptionState exceptionState(ExceptionState::SetterContext, *namedProperty, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% endif %}
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    {# v8_value_to_local_cpp_value('DOMString', 'nameString', 'propertyName') #}
    V8StringResource<> propertyName(nameString);
    if (!propertyName.prepare())
        return;
    {{v8_value_to_local_cpp_value(setter) | indent}}
    {% if setter.has_type_checking_interface %}
    {# Type checking for interface types (if interface not implemented, throw
       TypeError), per http://www.w3.org/TR/WebIDL/#es-interface #}
    if (!propertyValue{% if setter.is_nullable %} && !isUndefinedOrNull(v8Value){% endif %}) {
        exceptionState.throwTypeError("The provided value is not of type '{{setter.idl_type}}'.");
        exceptionState.throwIfNeeded();
        return;
    }
    {% endif %}
    {% if setter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% endif %}
    {% set setter_name = setter.name or 'anonymousNamedSetter' %}
    {% set setter_arguments = ['propertyName', 'propertyValue'] %}
    {% if setter.is_call_with_script_state %}
    {% set setter_arguments = ['scriptState'] + setter_arguments %}
    {% endif %}
    {% if setter.is_raises_exception %}
    {% set setter_arguments =  setter_arguments + ['exceptionState'] %}
    {% endif %}
    bool result = impl->{{setter_name}}({{setter_arguments | join(', ')}});
    {% if setter.is_raises_exception %}
    if (exceptionState.throwIfNeeded())
        return;
    {% endif %}
    if (!result)
        return;
    v8SetReturnValue(info, v8Value);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_setter_callback %}
{% if named_property_setter %}
{% set setter = named_property_setter %}
static void namedPropertySetterCallback(v8::Local<v8::Name> name, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<v8::Value>& info)
{
    {% if setter.is_custom %}
    {{v8_class}}::namedPropertySetterCustom(name, v8Value, info);
    {% else %}
    {{cpp_class}}V8Internal::namedPropertySetter(name, v8Value, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_query %}
{% if named_property_getter and named_property_getter.is_enumerable and
      not named_property_getter.is_custom_property_query %}
{% set getter = named_property_getter %}
{# If there is an enumerator, there MUST be a query method to properly
   communicate property attributes. #}
static void namedPropertyQuery(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info)
{
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    AtomicString propertyName = toCoreAtomicString(name.As<v8::String>());
    v8::String::Utf8Value namedProperty(name);
    ExceptionState exceptionState(ExceptionState::GetterContext, *namedProperty, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% set getter_arguments = ['propertyName', 'exceptionState'] %}
    {% if getter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% set getter_arguments = ['scriptState'] + getter_arguments %}
    {% endif %}
    bool result = impl->namedPropertyQuery({{getter_arguments | join(', ')}});
    if (exceptionState.throwIfNeeded())
        return;
    if (!result)
        return;
    v8SetReturnValueInt(info, v8::None);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_query_callback %}
{% if named_property_getter and named_property_getter.is_enumerable %}
{% set getter = named_property_getter %}
static void namedPropertyQueryCallback(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info)
{
    {% if getter.is_custom_property_query %}
    {{v8_class}}::namedPropertyQueryCustom(name, info);
    {% else %}
    {{cpp_class}}V8Internal::namedPropertyQuery(name, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_deleter %}
{% if named_property_deleter and not named_property_deleter.is_custom %}
{% set deleter = named_property_deleter %}
static void namedPropertyDeleter(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Boolean>& info)
{
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    AtomicString propertyName = toCoreAtomicString(name.As<v8::String>());
    {% if deleter.is_raises_exception %}
    v8::String::Utf8Value namedProperty(name);
    ExceptionState exceptionState(ExceptionState::DeletionContext, *namedProperty, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {% endif %}
    {% if deleter.is_call_with_script_state %}
    ScriptState* scriptState = ScriptState::current(info.GetIsolate());
    {% endif %}
    {% set deleter_name = deleter.name or 'anonymousNamedDeleter' %}
    {% set deleter_arguments = ['propertyName'] %}
    {% if deleter.is_call_with_script_state %}
    {% set deleter_arguments = ['scriptState'] + deleter_arguments %}
    {% endif %}
    {% if deleter.is_raises_exception %}
    {% set deleter_arguments = deleter_arguments + ['exceptionState'] %}
    {% endif %}
    DeleteResult result = impl->{{deleter_name}}({{deleter_arguments | join(', ')}});
    {% if deleter.is_raises_exception %}
    if (exceptionState.throwIfNeeded())
        return;
    {% endif %}
    if (result != DeleteUnknownProperty)
        return v8SetReturnValueBool(info, result == DeleteSuccess);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_deleter_callback %}
{% if named_property_deleter %}
{% set deleter = named_property_deleter %}
static void namedPropertyDeleterCallback(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Boolean>& info)
{
    {% if deleter.is_custom %}
    {{v8_class}}::namedPropertyDeleterCustom(name, info);
    {% else %}
    {{cpp_class}}V8Internal::namedPropertyDeleter(name, info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_enumerator %}
{% if named_property_getter and named_property_getter.is_enumerable and
      not named_property_getter.is_custom_property_enumerator %}
static void namedPropertyEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info)
{
    {{cpp_class}}* impl = {{v8_class}}::toImpl(info.Holder());
    Vector<String> names;
    ExceptionState exceptionState(ExceptionState::EnumerationContext, "{{interface_name}}", info.Holder(), info.GetIsolate());
    impl->namedPropertyEnumerator(names, exceptionState);
    if (exceptionState.throwIfNeeded())
        return;
    v8::Local<v8::Array> v8names = v8::Array::New(info.GetIsolate(), names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        if (!v8CallBoolean(v8names->Set(info.GetIsolate()->GetCurrentContext(), v8::Integer::New(info.GetIsolate(), i), v8String(info.GetIsolate(), names[i]))))
            return;
    }
    v8SetReturnValue(info, v8names);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_property_enumerator_callback %}
{% if named_property_getter and named_property_getter.is_enumerable %}
{% set getter = named_property_getter %}
static void namedPropertyEnumeratorCallback(const v8::PropertyCallbackInfo<v8::Array>& info)
{
    {% if getter.is_custom_property_enumerator %}
    {{v8_class}}::namedPropertyEnumeratorCustom(info);
    {% else %}
    {{cpp_class}}V8Internal::namedPropertyEnumerator(info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block origin_safe_method_setter %}
{% if has_origin_safe_method_setter %}
static void {{cpp_class}}OriginSafeMethodSetter(v8::Local<v8::Name> name, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<void>& info)
{
    v8::Local<v8::Object> holder = {{v8_class}}::findInstanceInPrototypeChain(info.This(), info.GetIsolate());
    if (holder.IsEmpty())
        return;
    {{cpp_class}}* impl = {{v8_class}}::toImpl(holder);
    v8::String::Utf8Value attributeName(name);
    ExceptionState exceptionState(ExceptionState::SetterContext, *attributeName, "{{interface_name}}", info.Holder(), info.GetIsolate());
    if (!BindingSecurity::shouldAllowAccessTo(info.GetIsolate(), callingDOMWindow(info.GetIsolate()), impl, exceptionState)) {
        exceptionState.throwIfNeeded();
        return;
    }

    {# The findInstanceInPrototypeChain() call above only returns a non-empty handle if info.This() is an Object. #}
    V8HiddenValue::setHiddenValue(ScriptState::current(info.GetIsolate()), v8::Local<v8::Object>::Cast(info.This()), name.As<v8::String>(), v8Value);
}

static void {{cpp_class}}OriginSafeMethodSetterCallback(v8::Local<v8::Name> name, v8::Local<v8::Value> v8Value, const v8::PropertyCallbackInfo<void>& info)
{
    {{cpp_class}}V8Internal::{{cpp_class}}OriginSafeMethodSetter(name, v8Value, info);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block named_constructor %}
{% from 'methods.cpp' import generate_constructor with context %}
{% if named_constructor %}
{% set to_active_scriptwrappable = '%s::toActiveScriptWrappable' % v8_class
                                   if active_scriptwrappable else '0' %}
// Suppress warning: global constructors, because struct WrapperTypeInfo is trivial
// and does not depend on another global objects.
#if defined(COMPONENT_BUILD) && defined(WIN32) && COMPILER(CLANG)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
const WrapperTypeInfo {{v8_class}}Constructor::wrapperTypeInfo = { gin::kEmbedderBlink, {{v8_class}}Constructor::domTemplate, {{v8_class}}::refObject, {{v8_class}}::derefObject, {{v8_class}}::trace, {{to_active_scriptwrappable}}, 0, {{v8_class}}::preparePrototypeAndInterfaceObject, {{v8_class}}::installConditionallyEnabledProperties, "{{interface_name}}", 0, WrapperTypeInfo::WrapperTypeObjectPrototype, WrapperTypeInfo::{{wrapper_class_id}}, WrapperTypeInfo::{{event_target_inheritance}}, WrapperTypeInfo::{{lifetime}}, WrapperTypeInfo::{{gc_type}} };
#if defined(COMPONENT_BUILD) && defined(WIN32) && COMPILER(CLANG)
#pragma clang diagnostic pop
#endif

{{generate_constructor(named_constructor)}}
v8::Local<v8::FunctionTemplate> {{v8_class}}Constructor::domTemplate(v8::Isolate* isolate)
{
    static int domTemplateKey; // This address is used for a key to look up the dom template.
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    v8::Local<v8::FunctionTemplate> result = data->existingDOMTemplate(&domTemplateKey);
    if (!result.IsEmpty())
        return result;

    result = v8::FunctionTemplate::New(isolate, {{v8_class}}ConstructorCallback);
    v8::Local<v8::ObjectTemplate> instanceTemplate = result->InstanceTemplate();
    instanceTemplate->SetInternalFieldCount({{v8_class}}::internalFieldCount);
    result->SetClassName(v8AtomicString(isolate, "{{cpp_class}}"));
    result->Inherit({{v8_class}}::domTemplate(isolate));
    data->setDOMTemplate(&domTemplateKey, result);
    return result;
}

{% endif %}
{% endblock %}

{##############################################################################}
{% block overloaded_constructor %}
{% if constructor_overloads %}
static void constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    ExceptionState exceptionState(ExceptionState::ConstructionContext, "{{interface_name}}", info.Holder(), info.GetIsolate());
    {# 2. Initialize argcount to be min(maxarg, n). #}
    switch (std::min({{constructor_overloads.maxarg}}, info.Length())) {
    {# 3. Remove from S all entries whose type list is not of length argcount. #}
    {% for length, tests_constructors in constructor_overloads.length_tests_methods %}
    case {{length}}:
        {# Then resolve by testing argument #}
        {% for test, constructor in tests_constructors %}
        {# 10. If i = d, then: #}
        if ({{test}}) {
            {{cpp_class}}V8Internal::constructor{{constructor.overload_index}}(info);
            return;
        }
        {% endfor %}
        break;
    {% endfor %}
    default:
        {# Invalid arity, throw error #}
        {# Report full list of valid arities if gaps and above minimum #}
        {% if constructor_overloads.valid_arities %}
        if (info.Length() >= {{constructor_overloads.length}}) {
            setArityTypeError(exceptionState, "{{constructor_overloads.valid_arities}}", info.Length());
            exceptionState.throwIfNeeded();
            return;
        }
        {% endif %}
        {# Otherwise just report "not enough arguments" #}
        exceptionState.throwTypeError(ExceptionMessages::notEnoughArguments({{constructor_overloads.length}}, info.Length()));
        exceptionState.throwIfNeeded();
        return;
    }
    {# No match, throw error #}
    exceptionState.throwTypeError("No matching constructor signature.");
    exceptionState.throwIfNeeded();
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block visit_dom_wrapper %}
{% if set_wrapper_reference_from or set_wrapper_reference_to %}
void {{v8_class}}::visitDOMWrapper(v8::Isolate* isolate, ScriptWrappable* scriptWrappable, const v8::Persistent<v8::Object>& wrapper)
{
    {{cpp_class}}* impl = scriptWrappable->toImpl<{{cpp_class}}>();
    {% if set_wrapper_reference_to %}
    v8::Local<v8::Object> context = v8::Local<v8::Object>::New(isolate, wrapper);
    v8::Context::Scope scope(context->CreationContext());
    {{set_wrapper_reference_to.cpp_type}} {{set_wrapper_reference_to.name}} = impl->{{set_wrapper_reference_to.name}}();
    if ({{set_wrapper_reference_to.name}}) {
        if (DOMDataStore::containsWrapper({{set_wrapper_reference_to.name}}, isolate))
            DOMDataStore::setWrapperReference(wrapper, {{set_wrapper_reference_to.name}}, isolate);
    }
    {% endif %}
    {% if set_wrapper_reference_from %}
    // The {{set_wrapper_reference_from}}() method may return a reference or a pointer.
    if (Node* owner = WTF::getPtr(impl->{{set_wrapper_reference_from}}())) {
        Node* root = V8GCController::opaqueRootForGC(isolate, owner);
        isolate->SetReferenceFromGroup(v8::UniqueId(reinterpret_cast<intptr_t>(root)), wrapper);
        return;
    }
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block constructor_callback %}
{% if constructors or has_custom_constructor or has_event_constructor %}
void {{v8_class}}::constructorCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    {% if measure_as %}
    UseCounter::countIfNotPrivateScript(info.GetIsolate(), currentExecutionContext(info.GetIsolate()), UseCounter::{{measure_as('Constructor')}});
    {% endif %}
    if (!info.IsConstructCall()) {
        V8ThrowException::throwTypeError(info.GetIsolate(), ExceptionMessages::constructorNotCallableAsFunction("{{interface_name}}"));
        return;
    }

    if (ConstructorMode::current(info.GetIsolate()) == ConstructorMode::WrapExistingObject) {
        v8SetReturnValue(info, info.Holder());
        return;
    }

    {% if has_custom_constructor %}
    {{v8_class}}::constructorCustom(info);
    {% else %}
    {{cpp_class}}V8Internal::constructor(info);
    {% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% macro install_do_not_check_security_method(method, world_suffix, instance_template, prototype_template) %}
{% from 'utilities.cpp' import property_location %}
{# Methods that are [DoNotCheckSecurity] are always readable, but if they are
   changed and then accessed from a different origin, we do not return the
   underlying value, but instead return a new copy of the original function.
   This is achieved by storing the changed value as a hidden property. #}
{% set getter_callback =
       '%sV8Internal::%sOriginSafeMethodGetterCallback%s' %
       (cpp_class, method.name, world_suffix) %}
{% set setter_callback =
       '{0}V8Internal::{0}OriginSafeMethodSetterCallback'.format(cpp_class)
       if not method.is_unforgeable else '0' %}
{% if method.is_per_world_bindings %}
{% set getter_callback_for_main_world = '%sForMainWorld' % getter_callback %}
{% set setter_callback_for_main_world = '%sForMainWorld' % setter_callback
       if not method.is_unforgeable else '0' %}
{% else %}
{% set getter_callback_for_main_world = '0' %}
{% set setter_callback_for_main_world = '0' %}
{% endif %}
{% set property_attribute =
       'static_cast<v8::PropertyAttribute>(%s)' %
       ' | '.join(method.property_attributes or ['v8::None']) %}
{% set only_exposed_to_private_script = 'V8DOMConfiguration::OnlyExposedToPrivateScript' if method.only_exposed_to_private_script else 'V8DOMConfiguration::ExposedToAllScripts' %}
{% set holder_check = 'V8DOMConfiguration::CheckHolder' %}
const V8DOMConfiguration::AttributeConfiguration {{method.name}}OriginSafeAttributeConfiguration = {
    "{{method.name}}", {{getter_callback}}, {{setter_callback}}, {{getter_callback_for_main_world}}, {{setter_callback_for_main_world}}, &{{v8_class}}::wrapperTypeInfo, v8::ALL_CAN_READ, {{property_attribute}}, {{only_exposed_to_private_script}}, {{property_location(method)}}, {{holder_check}},
};
V8DOMConfiguration::installAttribute(isolate, {{instance_template}}, {{prototype_template}}, {{method.name}}OriginSafeAttributeConfiguration);
{%- endmacro %}


{##############################################################################}
{% macro install_indexed_property_handler(target) %}
{% set indexed_property_getter_callback =
       '%sV8Internal::indexedPropertyGetterCallback' % cpp_class %}
{% set indexed_property_setter_callback =
       '%sV8Internal::indexedPropertySetterCallback' % cpp_class
       if indexed_property_setter else '0' %}
{% set indexed_property_query_callback = '0' %}{# Unused #}
{% set indexed_property_deleter_callback =
       '%sV8Internal::indexedPropertyDeleterCallback' % cpp_class
       if indexed_property_deleter else '0' %}
{% set indexed_property_enumerator_callback =
       'indexedPropertyEnumerator<%s>' % cpp_class
       if indexed_property_getter.is_enumerable else '0' %}
{% set property_handler_flags =
       'v8::PropertyHandlerFlags::kAllCanRead'
       if indexed_property_getter.do_not_check_security
       else 'v8::PropertyHandlerFlags::kNone' %}
v8::IndexedPropertyHandlerConfiguration indexedPropertyHandlerConfig({{indexed_property_getter_callback}}, {{indexed_property_setter_callback}}, {{indexed_property_query_callback}}, {{indexed_property_deleter_callback}}, {{indexed_property_enumerator_callback}}, v8::Local<v8::Value>(), {{property_handler_flags}});
{{target}}->SetHandler(indexedPropertyHandlerConfig);
{%- endmacro %}


{##############################################################################}
{% macro install_named_property_handler(target) %}
{% set named_property_getter_callback =
       '%sV8Internal::namedPropertyGetterCallback' % cpp_class %}
{% set named_property_setter_callback =
       '%sV8Internal::namedPropertySetterCallback' % cpp_class
       if named_property_setter else '0' %}
{% set named_property_query_callback =
       '%sV8Internal::namedPropertyQueryCallback' % cpp_class
       if named_property_getter.is_enumerable else '0' %}
{% set named_property_deleter_callback =
       '%sV8Internal::namedPropertyDeleterCallback' % cpp_class
       if named_property_deleter else '0' %}
{% set named_property_enumerator_callback =
       '%sV8Internal::namedPropertyEnumeratorCallback' % cpp_class
       if named_property_getter.is_enumerable else '0' %}
{% set property_handler_flags_list =
       ['int(v8::PropertyHandlerFlags::kOnlyInterceptStrings)'] %}
{% if named_property_getter.do_not_check_security %}
{% set property_handler_flags_list =
       property_handler_flags_list + ['int(v8::PropertyHandlerFlags::kAllCanRead)'] %}
{% endif %}
{% if not is_override_builtins %}
{% set property_handler_flags_list =
       property_handler_flags_list + ['int(v8::PropertyHandlerFlags::kNonMasking)'] %}
{% endif %}
{% set property_handler_flags =
       'static_cast<v8::PropertyHandlerFlags>(%s)' %
           ' | '.join(property_handler_flags_list) %}
v8::NamedPropertyHandlerConfiguration namedPropertyHandlerConfig({{named_property_getter_callback}}, {{named_property_setter_callback}}, {{named_property_query_callback}}, {{named_property_deleter_callback}}, {{named_property_enumerator_callback}}, v8::Local<v8::Value>(), {{property_handler_flags}});
{{target}}->SetHandler(namedPropertyHandlerConfig);
{%- endmacro %}


{##############################################################################}
{% block get_dom_template %}
{% if not is_array_buffer_or_view %}
v8::Local<v8::FunctionTemplate> {{v8_class}}::domTemplate(v8::Isolate* isolate)
{
    {% if has_partial_interface %}
    {% set installTemplateFunction = '%s::install%sTemplateFunction' % (v8_class, v8_class) %}
    ASSERT({{installTemplateFunction}} != {{v8_class}}::install{{v8_class}}Template);
    {% else %}
    {% set installTemplateFunction = 'install%sTemplate' % v8_class %}
    {% endif %}
{% set installTemplateFunction = '%s::install%sTemplateFunction' % (v8_class, v8_class) if has_partial_interface else 'install%sTemplate' % v8_class %}
    return V8DOMConfiguration::domClassTemplate(isolate, const_cast<WrapperTypeInfo*>(&wrapperTypeInfo), {{installTemplateFunction}});
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block get_dom_template_for_named_properties_object %}
{% if has_named_properties_object %}
v8::Local<v8::FunctionTemplate> {{v8_class}}::domTemplateForNamedPropertiesObject(v8::Isolate* isolate)
{
    v8::Local<v8::FunctionTemplate> parentTemplate = V8{{parent_interface}}::domTemplate(isolate);

    v8::Local<v8::FunctionTemplate> namedPropertiesObjectFunctionTemplate = v8::FunctionTemplate::New(isolate);
    namedPropertiesObjectFunctionTemplate->SetClassName(v8AtomicString(isolate, "{{interface_name}}Properties"));
    namedPropertiesObjectFunctionTemplate->Inherit(parentTemplate);

    v8::Local<v8::ObjectTemplate> namedPropertiesObjectTemplate = namedPropertiesObjectFunctionTemplate->PrototypeTemplate();
    namedPropertiesObjectTemplate->SetInternalFieldCount({{v8_class}}::internalFieldCount);
    V8DOMConfiguration::setClassString(isolate, namedPropertiesObjectTemplate, "{{interface_name}}Properties");
    {{install_named_property_handler('namedPropertiesObjectTemplate') | indent}}

    return namedPropertiesObjectFunctionTemplate;
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block has_instance %}
bool {{v8_class}}::hasInstance(v8::Local<v8::Value> v8Value, v8::Isolate* isolate)
{
    {% if is_array_buffer_or_view %}
    return v8Value->Is{{interface_name}}();
    {% else %}
    return V8PerIsolateData::from(isolate)->hasInstance(&wrapperTypeInfo, v8Value);
    {% endif %}
}

{% if not is_array_buffer_or_view %}
v8::Local<v8::Object> {{v8_class}}::findInstanceInPrototypeChain(v8::Local<v8::Value> v8Value, v8::Isolate* isolate)
{
    return V8PerIsolateData::from(isolate)->findInstanceInPrototypeChain(&wrapperTypeInfo, v8Value);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block to_impl %}
{% if interface_name == 'ArrayBuffer' or interface_name == 'SharedArrayBuffer' %}
{{cpp_class}}* V8{{interface_name}}::toImpl(v8::Local<v8::Object> object)
{
    ASSERT(object->Is{{interface_name}}());
    v8::Local<v8::{{interface_name}}> v8buffer = object.As<v8::{{interface_name}}>();
    if (v8buffer->IsExternal()) {
        const WrapperTypeInfo* wrapperTypeInfo = toWrapperTypeInfo(object);
        RELEASE_ASSERT(wrapperTypeInfo);
        RELEASE_ASSERT(wrapperTypeInfo->ginEmbedder == gin::kEmbedderBlink);
        return toScriptWrappable(object)->toImpl<{{cpp_class}}>();
    }

    // Transfer the ownership of the allocated memory to an {{interface_name}} without
    // copying.
    v8::{{interface_name}}::Contents v8Contents = v8buffer->Externalize();
    WTF::ArrayBufferContents contents(v8Contents.Data(), v8Contents.ByteLength(), WTF::ArrayBufferContents::{% if interface_name == 'ArrayBuffer' %}Not{% endif %}Shared);
    {{cpp_class}}* buffer = {{cpp_class}}::create(contents);
    v8::Local<v8::Object> associatedWrapper = buffer->associateWithWrapper(v8::Isolate::GetCurrent(), buffer->wrapperTypeInfo(), object);
    ASSERT_UNUSED(associatedWrapper, associatedWrapper == object);

    return buffer;
}

{% elif interface_name == 'ArrayBufferView' %}
{{cpp_class}}* V8ArrayBufferView::toImpl(v8::Local<v8::Object> object)
{
    ASSERT(object->IsArrayBufferView());
    ScriptWrappable* scriptWrappable = toScriptWrappable(object);
    if (scriptWrappable)
        return scriptWrappable->toImpl<{{cpp_class}}>();

    if (object->IsInt8Array())
        return V8Int8Array::toImpl(object);
    if (object->IsInt16Array())
        return V8Int16Array::toImpl(object);
    if (object->IsInt32Array())
        return V8Int32Array::toImpl(object);
    if (object->IsUint8Array())
        return V8Uint8Array::toImpl(object);
    if (object->IsUint8ClampedArray())
        return V8Uint8ClampedArray::toImpl(object);
    if (object->IsUint16Array())
        return V8Uint16Array::toImpl(object);
    if (object->IsUint32Array())
        return V8Uint32Array::toImpl(object);
    if (object->IsFloat32Array())
        return V8Float32Array::toImpl(object);
    if (object->IsFloat64Array())
        return V8Float64Array::toImpl(object);
    if (object->IsDataView())
        return V8DataView::toImpl(object);

    ASSERT_NOT_REACHED();
    return 0;
}

{% elif is_array_buffer_or_view %}
{{cpp_class}}* {{v8_class}}::toImpl(v8::Local<v8::Object> object)
{
    ASSERT(object->Is{{interface_name}}());
    ScriptWrappable* scriptWrappable = toScriptWrappable(object);
    if (scriptWrappable)
        return scriptWrappable->toImpl<{{cpp_class}}>();

    v8::Local<v8::{{interface_name}}> v8View = object.As<v8::{{interface_name}}>();
    v8::Local<v8::Object> arrayBuffer = v8View->Buffer();
    {{cpp_class}}* typedArray = nullptr;
    if (arrayBuffer->IsArrayBuffer()) {
        typedArray = {{cpp_class}}::create(V8ArrayBuffer::toImpl(arrayBuffer), v8View->ByteOffset(), v8View->{% if interface_name == 'DataView' %}Byte{% endif %}Length());
    } else if (arrayBuffer->IsSharedArrayBuffer()) {
        typedArray = {{cpp_class}}::create(V8SharedArrayBuffer::toImpl(arrayBuffer), v8View->ByteOffset(), v8View->{% if interface_name == 'DataView' %}Byte{% endif %}Length());
    } else {
        ASSERT_NOT_REACHED();
    }
    v8::Local<v8::Object> associatedWrapper = typedArray->associateWithWrapper(v8::Isolate::GetCurrent(), typedArray->wrapperTypeInfo(), object);
    ASSERT_UNUSED(associatedWrapper, associatedWrapper == object);

    return typedArray->toImpl<{{cpp_class}}>();
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block to_impl_with_type_check %}
{{cpp_class}}* {{v8_class}}::toImplWithTypeCheck(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    return hasInstance(value, isolate) ? toImpl(v8::Local<v8::Object>::Cast(value)) : 0;
}

{% endblock %}


{##############################################################################}
{% block install_conditional_attributes %}
{% from 'attributes.cpp' import attribute_configuration with context %}
{% if has_conditional_attributes_on_instance %}
void {{v8_class}}::installConditionallyEnabledProperties(v8::Local<v8::Object> instanceObject, v8::Isolate* isolate)
{
#error TODO(yukishiino): Rename this function to prepareInstanceObject (c.f. preparePrototypeAndInterfaceObject) and implement this function if necessary.  http://crbug.com/503508
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block prepare_prototype_and_interface_object %}
{% from 'methods.cpp' import install_conditionally_enabled_methods with context %}
{% if unscopeables or has_conditional_attributes_on_prototype or conditionally_enabled_methods %}
void {{v8_class}}::preparePrototypeAndInterfaceObject(v8::Local<v8::Context> context, v8::Local<v8::Object> prototypeObject, v8::Local<v8::Function> interfaceObject, v8::Local<v8::FunctionTemplate> interfaceTemplate)
{
    v8::Isolate* isolate = context->GetIsolate();
{% if unscopeables %}
    {{install_unscopeables() | indent}}
{% endif %}
{% if has_conditional_attributes_on_prototype %}
    {{install_conditionally_enabled_attributes_on_prototype() | indent}}
{% endif %}
{% if conditionally_enabled_methods %}
    {{install_conditionally_enabled_methods() | indent}}
{% endif %}
}

{% endif %}
{% endblock %}


{##############################################################################}
{% macro install_unscopeables() %}
v8::Local<v8::Name> unscopablesSymbol(v8::Symbol::GetUnscopables(isolate));
v8::Local<v8::Object> unscopeables;
if (v8CallBoolean(prototypeObject->HasOwnProperty(context, unscopablesSymbol)))
    unscopeables = prototypeObject->Get(context, unscopablesSymbol).ToLocalChecked().As<v8::Object>();
else
    unscopeables = v8::Object::New(isolate);
{% for name, runtime_enabled_function in unscopeables %}
{% filter runtime_enabled(runtime_enabled_function) %}
unscopeables->CreateDataProperty(context, v8AtomicString(isolate, "{{name}}"), v8::True(isolate)).FromJust();
{% endfilter %}
{% endfor %}
prototypeObject->CreateDataProperty(context, unscopablesSymbol, unscopeables).FromJust();
{% endmacro %}


{##############################################################################}
{% macro install_conditionally_enabled_attributes_on_prototype() %}
{% from 'attributes.cpp' import attribute_configuration with context %}
ExecutionContext* executionContext = toExecutionContext(context);
v8::Local<v8::Signature> signature = v8::Signature::New(isolate, interfaceTemplate);
{% for attribute in attributes if attribute.exposed_test and attribute.on_prototype %}
{% filter exposed(attribute.exposed_test) %}
const V8DOMConfiguration::AccessorConfiguration accessorConfiguration = {{attribute_configuration(attribute)}};
V8DOMConfiguration::installAccessor(isolate, v8::Local<v8::Object>(), prototypeObject, interfaceObject, signature, accessorConfiguration);
{% endfilter %}
{% endfor %}
{% endmacro %}


{##############################################################################}
{% block to_active_scriptwrappable %}
{% if active_scriptwrappable %}
ActiveScriptWrappable* {{v8_class}}::toActiveScriptWrappable(v8::Local<v8::Object> wrapper)
{
    return toImpl(wrapper);
}

{% endif %}
{% endblock %}


{##############################################################################}
{% block ref_object_and_deref_object %}
void {{v8_class}}::refObject(ScriptWrappable* scriptWrappable)
{
{% if gc_type == 'RefCountedObject' %}
    scriptWrappable->toImpl<{{cpp_class}}>()->ref();
{% endif %}
}

void {{v8_class}}::derefObject(ScriptWrappable* scriptWrappable)
{
{% if gc_type == 'RefCountedObject' %}
    scriptWrappable->toImpl<{{cpp_class}}>()->deref();
{% endif %}
}

{% endblock %}

{##############################################################################}
{% block partial_interface %}
{% if has_partial_interface %}
InstallTemplateFunction {{v8_class}}::install{{v8_class}}TemplateFunction = (InstallTemplateFunction)&{{v8_class}}::install{{v8_class}}Template;

void {{v8_class}}::updateWrapperTypeInfo(InstallTemplateFunction installTemplateFunction, PreparePrototypeAndInterfaceObjectFunction preparePrototypeAndInterfaceObjectFunction)
{
    {{v8_class}}::install{{v8_class}}TemplateFunction = installTemplateFunction;
    if (preparePrototypeAndInterfaceObjectFunction)
        {{v8_class}}::wrapperTypeInfo.preparePrototypeAndInterfaceObjectFunction = preparePrototypeAndInterfaceObjectFunction;
}

{% for method in methods if method.overloads and method.overloads.has_partial_overloads %}
void {{v8_class}}::register{{method.name | blink_capitalize}}MethodForPartialInterface(void (*method)(const v8::FunctionCallbackInfo<v8::Value>&))
{
    {{cpp_class}}V8Internal::{{method.name}}MethodForPartialInterface = method;
}

{% endfor %}
{% endif %}
{% endblock %}
