package ru.yandex.spark.yt.wrapper.discovery

import com.google.common.net.HostAndPort

case class Address(host: String, port: Int, webUiPort: Option[Int], restPort: Option[Int]) {
  def hostAndPort: HostAndPort = HostAndPort.fromParts(host, port)

  def webUiHostAndPort: HostAndPort = HostAndPort.fromParts(host, webUiPort.get)

  def restHostAndPort: HostAndPort = HostAndPort.fromParts(host, restPort.get)
}

object Address {
  def apply(hostAndPort: HostAndPort, webUiHostAndPort: HostAndPort, restHostAndPort: HostAndPort): Address = {
    Address(hostAndPort.getHostText, hostAndPort.getPort, Some(webUiHostAndPort.getPort), Some(restHostAndPort.getPort))
  }
}