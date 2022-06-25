#include <string>
#include <typeinfo>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace depInject
{

template<typename T>
struct ConstructorType
{
    typedef T Type;
};

#define INJECT(constructorFunction) \
typedef depInject::ConstructorType<constructorFunction> ConstructorTypedef; \
constructorFunction

template<typename T>
struct always_false
{
    enum { value = false };
};

template<typename T>
struct trim_shared_ptr;

template<typename T>
struct trim_shared_ptr<std::shared_ptr<T>>
{
    typedef T type;
};

template<typename T>
struct is_shared_ptr : public std::false_type {};

template<typename T>
struct is_shared_ptr<std::shared_ptr<T>> : public std::true_type {};

template <typename T>
struct has_constructor_injection
{
    typedef char true_type[1];
    typedef char false_type[2];

    template <typename C>
    static true_type& check(typename C::ConstructorTypedef*);

    template <typename>
    static false_type& check(...);

    static const bool value = sizeof(check<T>(0)) == sizeof(true_type);
};

template <typename T, typename = int>
struct has_name : std::false_type { };

template <typename T>
struct has_name<T, decltype((void)T::name(), 0)> : std::true_type {};

// Type helper
struct depInject_unspecified_component {};

struct component_type
{
    component_type(const std::type_info& t, const std::string& customName = "") : typeInfo(t), customName(customName) {}

    const std::type_info& typeInfo;
    const std::string customName;

    std::string name() const { return customName.empty() ? typeInfo.name() : customName; }
    bool specified() const { return typeInfo != typeid(depInject_unspecified_component); }
};


template<typename T>
static component_type make_component_type(const std::string& customName = "")
{
    return component_type(typeid(T), customName);
}

bool operator==(const component_type& first, const component_type& other)
{
    return first.typeInfo == other.typeInfo;
}

struct component_type_hash
{
    size_t operator()(const component_type& type) const
    {
        return type.typeInfo.hash_code();
    }
};

template<typename T, class Enable = void>
struct type_name
{
    static const char* value()
    {
        return typeid(T).name();
    }
};

template<typename T>
struct type_name<T, typename std::enable_if<has_name<T>::value>::type>
{
    static const char* value()
    {
        return T::name();
    }
};

// EXCEPTIONS
class CircularDependencyFound : public std::logic_error
{
public:
    explicit CircularDependencyFound(const component_type& type)
        : std::logic_error(std::string("Found circular dependency on object '") + type.name() + "'") {}
};

class ComponentNotFoundException : public std::logic_error
{
public:
    explicit ComponentNotFoundException(const component_type& type)
        : std::logic_error(std::string("Component for interface '") + type.name() + "' not found") {}
};

class Container;

class InjectionContext
{
public:
    InjectionContext(Container& container, component_type requesterComponent) : container_(container)
    { pushType(requesterComponent); }

    ~InjectionContext() { popType(); }
    Container& getContainer() { return container_; }
    void pushType(component_type& type) { componentStack_.emplace_back(type); }
    void popType() { componentStack_.pop_back(); }
    const std::vector<component_type>& getComponentStack() { return componentStack_; }

private:
    Container& container_;
    std::vector<component_type> componentStack_;
};


class ContextGuard
{
public:
    ContextGuard(InjectionContext* context, component_type type) : context_(context), type_(type)
    { context_->pushType(type); }

    ~ContextGuard() { context_->popType(); }

    void ensureNoCycle()
    {
        const std::vector<component_type>& stack = context_->getComponentStack();

        for (size_t i = 0; i < stack.size() - 1; ++i)
            if (stack[i] == stack.back())
                throw CircularDependencyFound(stack.back());
    }

private:
    InjectionContext* context_;
    component_type type_;
};

class IInstanceRetriever
{
public:
    virtual ~IInstanceRetriever() {}
};

template<typename TInterface>
class InstanceRetriever : public IInstanceRetriever
{
public:
    virtual std::shared_ptr<TInterface> forwardInstance(InjectionContext* context) = 0;
};

template<typename TImplementation, typename TInterface, typename TInstanceStorage>
class CastInstanceRetriever : public InstanceRetriever<TInterface>
{
public:
    CastInstanceRetriever(std::shared_ptr<TInstanceStorage> storage) :
        storage_(storage) {}

    virtual std::shared_ptr<TInterface> forwardInstance(InjectionContext* context) override
    { return std::dynamic_pointer_cast<TInterface>(storage_->getInstance(context)); }
private:
    std::shared_ptr<TInstanceStorage> storage_;
};

template<typename T>
class ComponentBuilder;

class Container
{
    template<typename TComponent>
    friend class ComponentBuilderBase;
public:
    Container() = default;
    Container(const Container* parentContainer) : parentContainer_(parentContainer) {}

    template<typename T>
    ComponentBuilder<T> bind() { return ComponentBuilder<T>(this); }

    template<typename TInterfaceWithSharedPtr> // container.get<std::shared_ptr<IFoo>>()
    typename std::enable_if<is_shared_ptr<TInterfaceWithSharedPtr>::value, TInterfaceWithSharedPtr>::type
    get(InjectionContext* context = nullptr)
    {
    	    return get<typename trim_shared_ptr<TInterfaceWithSharedPtr>::type>(context);
    }

    template<typename TInterface> // container.get<IFoo>()
    typename std::enable_if<!is_shared_ptr<TInterface>::value, std::shared_ptr<TInterface>>::type
    get(InjectionContext* context = nullptr)
    {
    	std::unique_ptr<InjectionContext> contextPtr;

	    if (context == nullptr)
	    {
	        contextPtr.reset(new InjectionContext(*this, make_component_type<depInject_unspecified_component>("Unspecified")));
	        context = contextPtr.get();
	    }

	    const component_type type = make_component_type<TInterface>();

	    std::vector<std::shared_ptr<IInstanceRetriever>> retrievers;
	    findInstanceRetrievers(retrievers, type);

	    if (retrievers.size() == 0)
	    	throw ComponentNotFoundException(type);

	    std::shared_ptr<InstanceRetriever<TInterface>> retriever = std::dynamic_pointer_cast<InstanceRetriever<TInterface>>(retrievers[0]);

	    return retriever->forwardInstance(context);
    }


private:
    void findInstanceRetrievers(std::vector<std::shared_ptr<IInstanceRetriever>>& instanceRetrievers, const component_type& type) const
    {
        auto iter = registrations_.find(type);
	    if (iter != registrations_.end())
	    {
	        const std::vector<std::shared_ptr<IInstanceRetriever>>& currentRetrievers = iter->second;
	        instanceRetrievers.insert(instanceRetrievers.end(), currentRetrievers.begin(), currentRetrievers.end());
	    }

	    if (parentContainer_ != nullptr)
	        parentContainer_->findInstanceRetrievers(instanceRetrievers, type);
    }

    const Container* parentContainer_ = nullptr;
    std::unordered_map<component_type, std::vector<std::shared_ptr<IInstanceRetriever>>, component_type_hash> registrations_;
};

template<typename TInstance>
struct ConstructorInvoker;

template<typename TInstance, typename ... TConstructorArgs>
struct ConstructorInvoker<TInstance(TConstructorArgs...)>
{
    static std::shared_ptr<TInstance> invoke(InjectionContext* context)
    {
        Container& container = context->getContainer();
        return std::make_shared<TInstance>(container.get<TConstructorArgs>(context)...);
    }
};

template<typename TInstance, class TEnable = void>
struct ConstructorFactory
{
    static_assert(always_false<TInstance>::value, "Missing INJECT macro on implementation type!");
};

template<typename TInstance>
struct ConstructorFactory<TInstance, typename std::enable_if<has_constructor_injection<TInstance>::value>::type>
{
    std::shared_ptr<TInstance> createInstance(InjectionContext* context)
    {
        return ConstructorInvoker<typename TInstance::ConstructorTypedef::Type>::invoke(context);
    }
};

template<typename TImplementation, typename TFactory>
class InstanceStorage
{
public:
    InstanceStorage(TFactory factory) :
        factory_(factory),
        mIsSingleton(false) {}

    virtual std::shared_ptr<TImplementation> getInstance(InjectionContext* context)
    {
        if (!mIsSingleton)
            return createInstance(context);

        if (mInstance == nullptr)
            mInstance = createInstance(context);

        return mInstance;
    }

    void setSingleton(bool value) { mIsSingleton = value; }

private:
    std::shared_ptr<TImplementation> createInstance(InjectionContext* context)
    {
        ContextGuard guard(context, make_component_type<TImplementation>(type_name<TImplementation>::value()));
        guard.ensureNoCycle();
        return factory_.createInstance(context);
    }

    TFactory factory_;
    bool mIsSingleton;
    std::shared_ptr<TImplementation> mInstance;
};

template<typename TInstanceStorage>
class StorageConfiguration
{
public:
    StorageConfiguration(std::shared_ptr<TInstanceStorage> storage) :
        storage_(storage) { }

    void InSingletonScope() { storage_->setSingleton(true); }

private:
    std::shared_ptr<TInstanceStorage> storage_;
};


template<typename TComponent>
class ComponentBuilderBase
{
public:
    ComponentBuilderBase(Container* container) :
        container_(container)
    {}

    template<typename TImplementation>
    StorageConfiguration<InstanceStorage<TImplementation, ConstructorFactory<TImplementation>>>
        to()
    {
        typedef InstanceStorage<TImplementation, ConstructorFactory<TImplementation>> InstanceStorageType;

        // Create instance holder
        auto instanceStorage = std::make_shared<InstanceStorageType>(ConstructorFactory<TImplementation>());

        registerType<TImplementation, InstanceStorageType>(instanceStorage);

        return StorageConfiguration<InstanceStorageType>(instanceStorage);
    }

private:
    template<typename TImplementation, typename TInstanceStorage>
    void registerType(std::shared_ptr<TInstanceStorage> instanceStorage)
    {
        static_assert(std::is_convertible<TImplementation*, TComponent*>::value, "No conversion exists from TImplementation* to TComponent*");

        container_->registrations_[make_component_type<TComponent>()]
            .emplace_back(std::shared_ptr<IInstanceRetriever>(new CastInstanceRetriever<TImplementation, TComponent, TInstanceStorage>(instanceStorage)));
    }

private:
    Container* container_;
};

// Specialization for single component registration that allows the ToSelf
template<typename TImplementation>
class ComponentBuilder : public ComponentBuilderBase<TImplementation>
{
public:
    ComponentBuilder(Container* container) :
        ComponentBuilderBase<TImplementation>(container) {}

    StorageConfiguration<InstanceStorage<TImplementation, ConstructorFactory<TImplementation>>> ToSelf()
    {
        return ComponentBuilderBase<TImplementation>::template to<TImplementation>();
    }
};

} // end of depInject namespace

