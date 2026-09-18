# kmixdeck D-Bus API (contract)

Bus: **session**. Name: **`org.kmixdeck1`** (the `1` is the API major version).

| Object path | Interfaces |
|---|---|
| `/org/kmixdeck1` | `org.kmixdeck1.Mixer`, `org.freedesktop.DBus.ObjectManager` |
| `/org/kmixdeck1/channel/<slug>` | `org.kmixdeck1.Channel` |
| `/org/kmixdeck1/mix/<slug>` | `org.kmixdeck1.Mix` |
| `/org/kmixdeck1/cell/<channel>/<mix>` | `org.kmixdeck1.Cell` |
| `/org/kmixdeck1/app/<id>` | `org.kmixdeck1.App` |

Every object also has `org.freedesktop.DBus.Properties` (with `PropertiesChanged`) and
`org.freedesktop.DBus.Introspectable`. Discover everything with one call:

```sh
busctl --user call org.kmixdeck1 /org/kmixdeck1 org.freedesktop.DBus.ObjectManager GetManagedObjects
busctl --user set-property org.kmixdeck1 /org/kmixdeck1/cell/game/stream org.kmixdeck1.Cell Volume d 0.25
busctl --user monitor org.kmixdeck1          # watch PropertiesChanged / InterfacesAdded live
```

Compatibility rule: within `org.kmixdeck1` members are only ever **added**. Anything else is
`org.kmixdeck2`, served in parallel. See ADR 0005 and `docs/frontend-guide.md`.
