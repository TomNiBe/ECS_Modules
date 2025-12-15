// Système entité‑composants minimaliste : définit les entités, les tableaux
// clairsemés et un registre pour gérer entités, composants et systèmes.  Les
// valeurs de jeu ne sont pas codées en dur et doivent provenir de données
// externes.

#pragma once

#include <any>
#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

// Déclarations anticipées des utilitaires de zip
namespace ecs {
template <typename... Arrays>
class zipper;
template <typename... Arrays>
class indexed_zipper;
} // namespace ecs

namespace ecs {

// Représente une entité par un indice numérique.  Une entité vide est
// invalide et les indices libérés sont réutilisés par le registre.
class entity_t {
public:
    using value_type = std::size_t;

    // Constructeur par défaut : entité invalide.
    entity_t() noexcept : _value{npos} {}

    // Constructeur explicite à partir d’un indice.
    explicit entity_t(value_type idx) noexcept : _value{idx} {}

    // Renvoie l’indice sous‑jacent.
    value_type value() const noexcept { return _value; }

    // Conversion booléenne : vrai si l’entité est valide.
    explicit operator bool() const noexcept { return _value != npos; }

    // Comparaison d’égalité et d’inégalité.
    friend bool operator==(entity_t const &a, entity_t const &b) noexcept {
        return a._value == b._value;
    }
    friend bool operator!=(entity_t const &a, entity_t const &b) noexcept {
        return !(a == b);
    }

private:
    static constexpr value_type npos = static_cast<value_type>(-1);
    value_type _value;
};

// Conteneur clairsemé de composants optionnels indexés par entité.
template <typename Component>
class sparse_array {
public:
    using value_type           = std::optional<Component>;
    using reference_type       = value_type &;
    using const_reference_type = value_type const &;
    using container_type       = std::vector<value_type>;
    using size_type            = typename container_type::size_type;

    sparse_array() = default;

    // Renvoie le nombre de cases (indice maximal + 1).
    size_type size() const noexcept { return _data.size(); }

    // Accès en lecture avec vérification des limites ; retourne vide hors bornes.
    const_reference_type operator[](entity_t e) const {
        size_type idx = e.value();
        if (idx >= _data.size()) {
            static const value_type empty{};
            return empty;
        }
        return _data[idx];
    }

    // Accès en écriture : redimensionne au besoin.
    reference_type operator[](entity_t e) {
        size_type idx = e.value();
        if (idx >= _data.size()) {
            _data.resize(idx + 1);
        }
        return _data[idx];
    }

    // Insère ou remplace un composant à l’indice donné.
    value_type &insert_at(entity_t e, const Component &c) {
        auto &slot = (*this)[e];
        slot = c;
        return slot;
    }

    // Construit un composant en place à l’indice donné.
    template <typename... Args>
    value_type &emplace_at(entity_t e, Args &&...args) {
        auto &slot = (*this)[e];
        slot.emplace(std::forward<Args>(args)...);
        return slot;
    }

    // Supprime le composant si présent.
    void erase(entity_t e) {
        size_type idx = e.value();
        if (idx < _data.size()) {
            _data[idx].reset();
        }
    }

    // Accès au stockage interne.
    container_type &data() noexcept { return _data; }
    const container_type &data() const noexcept { return _data; }

    // Itérateurs sur le stockage interne.
    auto begin() noexcept { return _data.begin(); }
    auto end() noexcept { return _data.end(); }
    auto begin() const noexcept { return _data.begin(); }
    auto end() const noexcept { return _data.end(); }

private:
    container_type _data;
};

// Registre central : gère les entités, les composants et les systèmes.
class registry {
public:
    using entity_type = entity_t;

    registry() = default;

    // Crée une nouvelle entité ; réutilise un indice libre si possible.
    entity_type spawn_entity() {
        if (!_free_ids.empty()) {
            auto id = _free_ids.back();
            _free_ids.pop_back();
            if (id >= _alive.size()) {
                _alive.resize(id + 1, true);
            } else {
                _alive[id] = true;
            }
            return entity_type{id};
        }
        auto id = static_cast<entity_type::value_type>(_alive.size());
        _alive.push_back(true);
        return entity_type{id};
    }

    // Supprime une entité et recycle son indice ; efface ses composants.
    void kill_entity(entity_type e) {
        auto id = e.value();
        if (id >= _alive.size() || !_alive[id]) {
            return;
        }
        _alive[id] = false;
        for (auto &erase_fn : _erasers) {
            erase_fn(*this, e);
        }
        _free_ids.push_back(id);
    }

    // Enregistre un type de composant et renvoie son tableau ; le crée si nécessaire.
    template <typename Component>
    sparse_array<Component> &register_component() {
        std::type_index ti{typeid(Component)};
        auto it = _components.find(ti);
        if (it == _components.end()) {
            auto [iter, _] =
                _components.emplace(ti, std::any(sparse_array<Component>{}));
            // crée une fonction d’effacement capturant l’identifiant du type
            _erasers.emplace_back([](registry &r, entity_type ent) {
                r.template get_components<Component>().erase(ent);
            });
            return std::any_cast<sparse_array<Component> &>(iter->second);
        }
        return std::any_cast<sparse_array<Component> &>(it->second);
    }

    // Renvoie le tableau du composant ; l’enregistre au besoin.
    template <typename Component>
    sparse_array<Component> &get_components() {
        std::type_index ti{typeid(Component)};
        auto it = _components.find(ti);
        if (it == _components.end()) {
            return register_component<Component>();
        }
        return std::any_cast<sparse_array<Component> &>(it->second);
    }

    // Version const de get_components().
    template <typename Component>
    const sparse_array<Component> &get_components() const {
        std::type_index ti{typeid(Component)};
        auto it = _components.find(ti);
        if (it == _components.end()) {
            static const sparse_array<Component> empty{};
            return empty;
        }
        return std::any_cast<const sparse_array<Component> &>(it->second);
    }

    // Ajoute un composant à une entité.
    template <typename Component>
    typename sparse_array<Component>::reference_type add_component(entity_type e,
                                                                   Component &&c) {
        auto &arr = get_components<Component>();
        return arr.insert_at(e, std::forward<Component>(c));
    }

    // Construit un composant en place pour une entité.
    template <typename Component, typename... Args>
    typename sparse_array<Component>::reference_type emplace_component(entity_type e,
                                                                      Args &&...args) {
        auto &arr = get_components<Component>();
        return arr.emplace_at(e, std::forward<Args>(args)...);
    }

    // Supprime un composant de l’entité si présent.
    template <typename Component>
    void remove_component(entity_type e) {
        auto &arr = get_components<Component>();
        arr.erase(e);
    }

    // Enregistre un système ; l’ordre d’enregistrement définit l’ordre d’exécution.
    template <typename... Components, typename Function>
    void add_system(Function &&f) {
        auto wrapper = [fn = std::forward<Function>(f)](registry &r) {
            fn(r, r.template get_components<Components>()...);
        };
        _systems.emplace_back(std::move(wrapper));
    }

    // Exécute tous les systèmes enregistrés.
    void run_systems() {
        for (auto &sys : _systems) {
            sys(*this);
        }
    }

private:
    // Indique si un indice d’entité est vivant.  Au‑delà de la taille, les entités sont considérées mortes.
    std::vector<bool> _alive;
    // Liste des indices d’entités libres à réutiliser.
    std::vector<entity_type::value_type> _free_ids;
    // Tableaux clairsemés indexés par type ; std::any permet l’effacement de type tout en conservant le conteneur.
    std::unordered_map<std::type_index, std::any> _components;
    // Fonctions pour effacer un composant d’une entité ; une par type de composant enregistré.
    std::vector<std::function<void(registry &, entity_type)>> _erasers;
    // Enveloppes de systèmes enregistrés ; elles capturent l’appelable utilisateur et extraient les composants requis à l’appel.
    std::vector<std::function<void(registry &)>> _systems;
};

// Déclarations des utilitaires de zip ; les implémentations sont dans zipper.hpp.
template <typename... Arrays>
class zipper;
template <typename... Arrays>
class indexed_zipper;
// Fonctions libres retournant un zip ou un zip indexé
template <typename... Arrays>
auto zip(Arrays &... arrays);
template <typename... Arrays>
auto indexed_zip(Arrays &... arrays);

} // namespace ecs