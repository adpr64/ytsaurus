package org.apache.spark.metrics.sink

import com.codahale.metrics.{Counter, Gauge, Histogram, Meter, MetricRegistry, Timer, UniformReservoir}
import io.circe.{Json, parser}
import org.apache.spark.yt.test.TestHttpServer
import org.apache.spark.yt.test.TestHttpServer.Request
import org.scalatest.{Assertion, FunSuite}
import org.scalatest.Matchers._
import org.slf4j.{Logger, LoggerFactory}

import java.nio.charset.Charset
import java.util.Properties
import java.util.concurrent.TimeUnit

class SolomonSinkTest extends FunSuite {
  val log: Logger = LoggerFactory.getLogger(this.getClass)

  def body(req: Request): String = new String(req.body, Charset.defaultCharset)
  def json(req: Request): Json = parser.parse(body(req)).right.get


  def checkSink(prepareMetrics: MetricRegistry => Unit)(assert: Json => Unit): Assertion = {
    val server = TestHttpServer()
    val registry: MetricRegistry = new MetricRegistry()
    prepareMetrics(registry)
    val props: Properties = new Properties()
    try {
      server.start()
      val port = server.port
      props.setProperty("solomon_port", port.toString)
      val sink = new SolomonSink(props, registry, null)
      server.assert(req => assert(json(req)))
      sink.report()
      server.awaitResult().httpStatusCode should be(200)
    } finally {
      server.stop()
    }
  }

  test("test empty metrics") {
    checkSink(_ => {}) { json =>
      json.hcursor.downField("metrics").as[List[Json]].right.get.size shouldBe 0
      json.hcursor.downField("commonLabels").as[Json].right.get.asObject.get.isEmpty shouldBe true
      json.hcursor.downField("ts").as[Long].right.get should be > 0L
      json.hcursor.downField("ts").as[Long].right.get should be < (System.currentTimeMillis() / 1000L + 100)
    }
  }

  test("test gauges") {
    checkSink(reg => {
      reg.register("gauge1", new Gauge[Int] {
        override def getValue: Int = 1
      })
      reg.register("gauge2", new Gauge[Float] {
        override def getValue: Float = 2
      })
      reg.register("gauge3", new Gauge[Long] {
        override def getValue: Long = 3
      })
      reg.register("gauge4", new Gauge[Double] {
        override def getValue: Double = 4
      })
    })(json => {
      val metrics = json.hcursor.downField("metrics").as[List[Json]].right.get
      metrics.size shouldBe 4
      metrics.head.hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "gauge1")
      metrics.head.hcursor.downField("type").as[String].right.get shouldBe "IGAUGE"
      metrics.head.hcursor.downField("value").as[Int].right.get shouldBe 1
      metrics(1).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "gauge2")
      metrics(1).hcursor.downField("type").as[String].right.get shouldBe "DGAUGE"
      metrics(1).hcursor.downField("value").as[Float].right.get shouldBe 2.0F
      metrics(2).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "gauge3")
      metrics(2).hcursor.downField("type").as[String].right.get shouldBe "IGAUGE"
      metrics(2).hcursor.downField("value").as[Long].right.get shouldBe 3L
      metrics(3).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "gauge4")
      metrics(3).hcursor.downField("type").as[String].right.get shouldBe "DGAUGE"
      metrics(3).hcursor.downField("value").as[Double].right.get shouldBe 4.0D
    })
  }

  test("test counter") {
    checkSink(reg => {
      val counter = new Counter
      reg.register("counter1", counter)
      counter.inc(3)
    })(json => {
      val metrics = json.hcursor.downField("metrics").as[List[Json]].right.get
      metrics.size shouldBe 1
      metrics.head.hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "counter1")
      metrics.head.hcursor.downField("type").as[String].right.get shouldBe "COUNTER"
      metrics.head.hcursor.downField("value").as[Long].right.get shouldBe 3L
    })
  }

  test("test meter") {
    checkSink(reg => {
      val meter = new Meter
      reg.register("meter1", meter)
      meter.mark(5L)
      meter.mark(4L)
      meter.mark(3L)
    })(json => {
      val metrics = json.hcursor.downField("metrics").as[List[Json]].right.get
      metrics.size shouldBe 5
      metrics.head.hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "meter1.count")
      metrics.head.hcursor.downField("type").as[String].right.get shouldBe "COUNTER"
      metrics.head.hcursor.downField("value").as[Long].right.get shouldBe 12L
      metrics(1).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "meter1.mean_rate")
      metrics(1).hcursor.downField("type").as[String].right.get shouldBe "DGAUGE"
      metrics(1).hcursor.downField("value").as[Double].right.get should be > 0D
      metrics(2).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "meter1.rate_1min")
      metrics(2).hcursor.downField("type").as[String].right.get shouldBe "DGAUGE"
      metrics(3).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "meter1.rate_5min")
      metrics(3).hcursor.downField("type").as[String].right.get shouldBe "DGAUGE"
      metrics(4).hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "meter1.rate_15min")
      metrics(4).hcursor.downField("type").as[String].right.get shouldBe "DGAUGE"
    })
  }

  test("test histogram") {
    checkSink(reg => {
      val hist1 = new Histogram(new UniformReservoir())
      reg.register("hist1", hist1)
      hist1.update(1)
      hist1.update(2)
      hist1.update(3)
    })(json => {
      val metrics = json.hcursor.downField("metrics").as[List[Json]].right.get
      metrics.size shouldBe 9
      metrics.head.hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "hist1.count")
      metrics.head.hcursor.downField("type").as[String].right.get shouldBe "COUNTER"
      metrics.head.hcursor.downField("value").as[Long].right.get shouldBe 3L
      metrics.tail.map(_.hcursor.downField("labels").as[Map[String, String]].right.get).map(_ ("name"))
        .toSet shouldBe Set("hist1.max", "hist1.mean", "hist1.median", "hist1.min", "hist1.stddev", "hist1.p75",
          "hist1.p95", "hist1.p99")
      metrics.tail.map(_.hcursor.downField("type").as[String].right.get).foreach(_ shouldBe "DGAUGE")
    })
  }

  test("test timer") {
    checkSink(reg => {
      val timer1 = new Timer
      reg.register("timer1", timer1)
      timer1.update(1, TimeUnit.SECONDS)
      timer1.update(2, TimeUnit.SECONDS)
      timer1.update(3, TimeUnit.SECONDS)
    })(json => {
      val metrics = json.hcursor.downField("metrics").as[List[Json]].right.get
      metrics.size shouldBe 13
      metrics.head.hcursor.downField("labels").as[Map[String, String]].right.get shouldBe Map("name" -> "timer1.count")
      metrics.head.hcursor.downField("type").as[String].right.get shouldBe "COUNTER"
      metrics.head.hcursor.downField("value").as[Long].right.get shouldBe 3L
      metrics.tail.map(_.hcursor.downField("labels").as[Map[String, String]].right.get).map(_ ("name"))
        .toSet shouldBe Set("timer1.max", "timer1.mean", "timer1.median", "timer1.min", "timer1.stddev", "timer1.p75",
        "timer1.p95", "timer1.p99", "timer1.mean_rate", "timer1.rate_1min", "timer1.rate_5min", "timer1.rate_15min")
      metrics.tail.map(_.hcursor.downField("type").as[String].right.get).foreach(_ shouldBe "DGAUGE")
    })
  }
}
