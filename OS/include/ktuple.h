#pragma once

/*
    Functionality
        Initialization
            ktuple<int, int, kstring> tuple(2, 3, str);

        Indexing
            tuple.get<i>();
*/

template <typename T, typename... Rest>
struct TupleNode : TupleNode<Rest...>
{
    using Base = TupleNode<Rest...>; 
    T value;

    TupleNode(const T& val, const Rest&... rest) : TupleNode<Rest...>(rest...), value(val) {}
};

template <typename T>
struct TupleNode<T>
{
    using Base = void;
    T value;

    TupleNode(const T& val) : value(val) {}
};

namespace collections
{
    template <typename... Types>
    class ktuple : public TupleNode<Types...>
    {
    private:
        template <unsigned int Index, typename Node>
        struct GetHelper
        {
            static auto& get(Node& node)
            {
                return GetHelper<Index - 1, typename Node::Base>::get(node); 
            }
        };

        template <typename Node>
        struct GetHelper<0, Node>
        {
            static auto& get(Node& node)
            {
                return node.value; 
            }
        };

    public:
        ktuple(const Types&... args) : TupleNode<Types...>(args...) {}

        template <unsigned int Index>
        auto& get()
        {
            return GetHelper<Index, ktuple>::get(*this);
        }
    };
}
