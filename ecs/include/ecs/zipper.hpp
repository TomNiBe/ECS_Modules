// Utilitaires pour parcourir plusieurs tableaux clairsemés en parallèle.  Un
// zipper permet d’itérer en synchronisation sur plusieurs tableaux en sautant
// les indices dépourvus de composants ; indexed_zipper renvoie en plus l’indice.

#pragma once

#include <cstddef>
#include <tuple>
#include <utility>
#include <type_traits>

#include "ecs/ecs.hpp" // pour entity_t et sparse_array

namespace ecs {

// Déclaration anticipée du registre pour éviter les inclusions circulaires ;
// non requise ici.

// Métafonction interne pour déduire le type de référence d’un composant dans un tableau.
namespace detail {
    template <typename Array>
    using component_ref_t = decltype(std::declval<Array>()[entity_t{0}].value());
}

// Itérateur parallèle sur plusieurs tableaux clairsemés ; saute les indices où un composant est manquant.
template <typename... Arrays>
class zipper {
public:
    using reference = std::tuple<detail::component_ref_t<Arrays>&...>;
    using value_type = reference;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    // Construit un zipper à partir de plusieurs tableaux (stockés par pointeur, non possédés).  La limite d’itération est la taille maximale.
    explicit zipper(Arrays&... arrays)
        : _arrays(std::addressof(arrays)...),
          _max_size(max_size(arrays...)) {}

    // Classe d’itérateur interne respectant l’interface d’un itérateur d’entrée ;
    // elle maintient l’indice courant et avance jusqu’au prochain index où tous
    // les composants sont présents.
    class iterator {
    public:
        iterator(std::size_t index,
                 std::size_t max,
                 std::tuple<Arrays*...> arrays)
            : _index(index),
              _max(max),
              _arrays(std::move(arrays)) {
            skip_invalid();
        }

        // Pré‑incrément : avance jusqu’au prochain index valide.
        iterator& operator++() {
            ++_index;
            skip_invalid();
            return *this;
        }

        // Post‑incrément : renvoie une copie avant l’incrément.
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        // Comparaison d’égalité : mêmes indices supposent le même conteneur.
        friend bool operator==(const iterator& lhs, const iterator& rhs) {
            return lhs._index == rhs._index;
        }

        // Comparaison d’inégalité.
        friend bool operator!=(const iterator& lhs, const iterator& rhs) {
            return !(lhs == rhs);
        }

        // Déréférence : renvoie un tuple de références aux composants présents.
        reference operator*() const {
            return deref(std::index_sequence_for<Arrays...>{});
        }

        reference operator->() const {
            return **this;
        }

    private:
        // Déréférencie récursivement chaque tableau à l’indice courant.
        template <std::size_t... Is>
        reference deref(std::index_sequence<Is...>) const {
            // entity_t construit à partir de l’indice courant (conversion explicite)
            entity_t ent{_index};
            return std::forward_as_tuple(((*std::get<Is>(_arrays))[ent].value())...);
        }

        // Vérifie que tous les tableaux possèdent un composant à l’indice courant.
        template <std::size_t... Is>
        bool all_present(std::index_sequence<Is...>) const {
            entity_t ent{_index};
            bool ok = true;
            (void)std::initializer_list<int>{
                ([&]() {
                    auto* arr = std::get<Is>(_arrays);
                    if (ent.value() >= arr->size() || !((*arr)[ent].has_value())) {
                        ok = false;
                    }
                    return 0;
                }(),
                0)...};
            return ok;
        }

        // Avance l’indice jusqu’à trouver un ensemble valide ou atteindre la fin.
        void skip_invalid() {
            while (_index < _max) {
                if (all_present(std::index_sequence_for<Arrays...>{})) {
                    return;
                }
                ++_index;
            }
        }

        std::size_t              _index;
        std::size_t              _max;
        std::tuple<Arrays*...>   _arrays;
    };

    // Renvoie un itérateur sur le premier élément valide.
    iterator begin() {
        return iterator{0, _max_size, _arrays};
    }

    // Renvoie un itérateur de fin.
    iterator end() {
        return iterator{_max_size, _max_size, _arrays};
    }

private:
    // Calcule la taille maximale parmi tous les tableaux (pli en temps de compilation).
    static std::size_t max_size(Arrays const &...arrs) {
        std::size_t max{0};
        (void)std::initializer_list<int>{
            (max = (std::max)(max, arrs.size()), 0)...};
        return max;
    }

    std::tuple<Arrays*...> _arrays;
    std::size_t            _max_size;
};

// indexed_zipper : variante de zipper qui renvoie également l’indice courant en
// plus des références aux composants.  Utile pour obtenir le handle d’entité.
template <typename... Arrays>
class indexed_zipper {
public:
    using reference = std::tuple<std::size_t, detail::component_ref_t<Arrays>&...>;
    using value_type = reference;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    explicit indexed_zipper(Arrays&... arrays)
        : _arrays(std::addressof(arrays)...),
          _max_size(zipper<Arrays...>::max_size(arrays...)) {}

    class iterator {
    public:
        iterator(std::size_t index,
                 std::size_t max,
                 std::tuple<Arrays*...> arrays)
            : _index(index),
              _max(max),
              _arrays(std::move(arrays)) {
            skip_invalid();
        }

        iterator& operator++() {
            ++_index;
            skip_invalid();
            return *this;
        }
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        friend bool operator==(iterator const &a, iterator const &b) {
            return a._index == b._index;
        }
        friend bool operator!=(iterator const &a, iterator const &b) {
            return !(a == b);
        }
        reference operator*() const {
            return deref(std::index_sequence_for<Arrays...>{});
        }
        reference operator->() const {
            return **this;
        }

    private:
        template <std::size_t... Is>
        reference deref(std::index_sequence<Is...>) const {
            entity_t ent{_index};
            return std::tuple< std::size_t, detail::component_ref_t<Arrays>&... >{
                _index, ((*std::get<Is>(_arrays))[ent].value())... };
        }
        template <std::size_t... Is>
        bool all_present(std::index_sequence<Is...>) const {
            entity_t ent{_index};
            bool ok = true;
            (void)std::initializer_list<int>{
                ([&]() {
                    auto* arr = std::get<Is>(_arrays);
                    if (ent.value() >= arr->size() || !((*arr)[ent].has_value())) {
                        ok = false;
                    }
                    return 0;
                }(),
                0)...};
            return ok;
        }
        void skip_invalid() {
            while (_index < _max) {
                if (all_present(std::index_sequence_for<Arrays...>{})) {
                    return;
                }
                ++_index;
            }
        }
        std::size_t            _index;
        std::size_t            _max;
        std::tuple<Arrays*...> _arrays;
    };

    iterator begin() {
        return iterator{0, _max_size, _arrays};
    }
    iterator end() {
        return iterator{_max_size, _max_size, _arrays};
    }

private:
    std::tuple<Arrays*...> _arrays;
    std::size_t            _max_size;
};

// Fonctions utilitaires pour construire un zip ou un zip indexé à partir de
// plusieurs tableaux ; les paramètres sont déduits automatiquement et un
// conteneur adapté est retourné.

template <typename... Arrays>
auto zip(Arrays&... arrays) {
    return zipper<Arrays...>(arrays...);
}

template <typename... Arrays>
auto indexed_zip(Arrays&... arrays) {
    return indexed_zipper<Arrays...>(arrays...);
}

} // namespace ecs