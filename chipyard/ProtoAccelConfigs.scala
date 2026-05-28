package chipyard

import org.chipsalliance.cde.config.{Config}

// ------------------------------
// ProtoAcc (protobuf serialize/deserialize RoCC accelerator) configs
// WithProtoAccel adds both the deserializer (custom2) and serializer (custom3).
// ------------------------------

// Rocket bring-up config (simplest to elaborate/simulate)
class ProtoAccelRocketConfig extends Config(
  new protoacc.WithProtoAccel ++                                  // protoacc deserializer + serializer
  new chipyard.config.WithExtMemIdBits(7) ++                      // widen mem req id bits for many outstanding reqs
  new chipyard.config.WithSystemBusWidth(128) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)

// BOOM config mirroring the original AE setup (MegaBoom + memory tuning)
class ProtoAccelMegaBoomConfig extends Config(
  new protoacc.WithProtoAccel ++                                  // protoacc deserializer + serializer
  new chipyard.config.WithExtMemIdBits(7) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new freechips.rocketchip.subsystem.WithNBanks(8) ++
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=16, capacityKB=2048) ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(4) ++
  new boom.v3.common.WithNMegaBooms(1) ++
  new chipyard.config.AbstractConfig)

// Split builds matching the AE (serializer-only / deserializer-only on MegaBoom)
class ProtoAccelSerMegaBoomConfig extends Config(
  new protoacc.WithProtoAccelSerOnly ++
  new chipyard.config.WithExtMemIdBits(7) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new freechips.rocketchip.subsystem.WithNBanks(8) ++
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=16, capacityKB=2048) ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(4) ++
  new boom.v3.common.WithNMegaBooms(1) ++
  new chipyard.config.AbstractConfig)

class ProtoAccelDeserMegaBoomConfig extends Config(
  new protoacc.WithProtoAccelDeserOnly ++
  new chipyard.config.WithExtMemIdBits(7) ++
  new chipyard.config.WithSystemBusWidth(128) ++
  new freechips.rocketchip.subsystem.WithNBanks(8) ++
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=16, capacityKB=2048) ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(4) ++
  new boom.v3.common.WithNMegaBooms(1) ++
  new chipyard.config.AbstractConfig)
