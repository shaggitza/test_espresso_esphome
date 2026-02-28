import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]

espresso_machine_sprofiler_ns = cg.esphome_ns.namespace(
    "espresso_machine_sprofiler"
)
SprofilerShotUpload = espresso_machine_sprofiler_ns.class_(
    "SprofilerShotUpload", cg.Component
)

CONF_SERVER = "server"
CONF_API_TOKEN = "api_token"
CONF_PROFILE_NAME = "profile_name"
CONF_ESPRESSO_MACHINE = "espresso_machine"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SprofilerShotUpload),
        # Server URL — defaults to https://sprofiler.io; users can point to a
        # self-hosted instance or the Sprofiler dev server.
        cv.Optional(
            CONF_SERVER, default="https://sprofiler.io"
        ): cv.url,
        # Bearer token obtained from the user's Sprofiler account settings.
        cv.Required(CONF_API_TOKEN): cv.string,
        # Human-readable profile name included in uploaded shot records.
        cv.Optional(
            CONF_PROFILE_NAME, default="Manual"
        ): cv.string,
        # Reference to the espresso_machine orchestrator.  When set the
        # sprofiler automatically records and uploads shots on brew end.
        cv.Optional(CONF_ESPRESSO_MACHINE): cv.use_id(cg.Component),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_server_url(config[CONF_SERVER]))
    cg.add(var.set_api_token(config[CONF_API_TOKEN]))
    cg.add(var.set_profile_name(config[CONF_PROFILE_NAME]))

    if CONF_ESPRESSO_MACHINE in config:
        machine = await cg.get_variable(config[CONF_ESPRESSO_MACHINE])
        cg.add(var.set_machine(machine))
